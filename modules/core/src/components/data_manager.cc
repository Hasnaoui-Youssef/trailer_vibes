#include "core/components/data_manager.hpp"


#include "core/components/execution_controller.hpp"
#include "core/components/target_manager.hpp"
#include "core/debug_context.hpp"
#include "core/lldb_utils.hpp"
#include "core/variable_description.hpp"
#include "dap/dap_error.hpp"
#include "dap/json_utils.hpp"
#include "dap/protocol/protocol_events.hpp"
#include "dap/protocol_support.hpp"
#include "lldb/API/SBAddress.h"
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBDeclaration.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBInstructionList.h"
#include "lldb/API/SBLineEntry.h"
#include "lldb/API/SBModule.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStringList.h"
#include "lldb/API/SBThread.h"
#include "lldb/API/SBType.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/FormatVariadic.h"
#include <array>
#include <tuple>

namespace core {

namespace {

constexpr int kStackPageSize = 20;

protocol::StackFrame CreateStackFrame(DebugContext &context, lldb::SBFrame &frame, lldb::SBFormat &format) {
  protocol::StackFrame stack_frame;
  stack_frame.id = MakeDAPFrameID(frame);

  lldb::SBStream stream;
  if (format && frame.GetDescriptionWithFormat(format, stream).Success()) {
    stack_frame.name = llvm::StringRef(stream.GetData(), stream.GetSize());
  } else if (llvm::StringRef name = frame.GetDisplayFunctionName(); !name.empty()) {
    stack_frame.name = name;
  }

  if (stack_frame.name.empty())
    stack_frame.name = GetLoadAddressString(frame.GetPC());

  if (!format && frame.GetFunction().GetIsOptimized())
    stack_frame.name += " [opt]";

  std::optional<protocol::Source> source = context.ResolveSource(frame);
  if (source && !IsAssemblySource(*source)) {
    auto line_entry = frame.GetLineEntry();
    stack_frame.line = line_entry.GetLine();
    stack_frame.column = line_entry.GetColumn();
  } else if (frame.GetSymbol().IsValid()) {
    lldb::SBInstructionList inst_list =
        context.Target().ReadInstructions(frame.GetSymbol().GetStartAddress(), frame.GetPCAddress(), nullptr);
    stack_frame.line = inst_list.GetSize() + 1;
    stack_frame.column = 1;
  } else {
    stack_frame.line = 0;
    stack_frame.column = 0;
  }

  stack_frame.source = std::move(source);
  stack_frame.instructionPointerReference = frame.GetPC();

  if (frame.IsArtificial() || frame.IsHidden())
    stack_frame.presentationHint = protocol::StackFrame::ePresentationHintSubtle;
  if (const lldb::SBModule module = frame.GetModule()) {
    if (llvm::StringRef uuid = module.GetUUIDString(); !uuid.empty())
      stack_frame.moduleId = uuid.str();
  }

  return stack_frame;
}

protocol::StackFrame CreateExtendedStackFrameLabel(lldb::SBThread &thread, lldb::SBFormat &format) {
  protocol::StackFrame stack_frame;
  lldb::SBStream stream;
  if (format && thread.GetDescriptionWithFormat(format, stream).Success()) {
    stack_frame.name = llvm::StringRef(stream.GetData(), stream.GetSize());
  } else {
    const uint32_t thread_idx = thread.GetExtendedBacktraceOriginatingIndexID();
    if (llvm::StringRef queue_name = thread.GetQueueName(); !queue_name.empty())
      stack_frame.name = llvm::formatv("Enqueued from {0} (Thread {1})", queue_name, thread_idx);
    else
      stack_frame.name = llvm::formatv("Thread {0}", thread_idx);
  }

  stack_frame.id = thread.GetThreadID() + 1;
  stack_frame.presentationHint = protocol::StackFrame::ePresentationHintLabel;
  stack_frame.line = 0;
  stack_frame.column = 0;

  return stack_frame;
}

bool FillStackFrames(DebugContext &context, lldb::SBThread &thread, lldb::SBFormat &frame_format,
                     std::vector<protocol::StackFrame> &stack_frames, int64_t &offset, const int64_t start_frame,
                     const int64_t levels, const bool include_all) {
  bool reached_end_of_stack = false;
  for (int64_t i = start_frame; static_cast<int64_t>(stack_frames.size()) < levels; i++) {
    if (i == -1) {
      stack_frames.emplace_back(CreateExtendedStackFrameLabel(thread, frame_format));
      continue;
    }

    lldb::SBFrame frame = thread.GetFrameAtIndex(i);
    if (!frame.IsValid()) {
      offset += thread.GetNumFrames() + 1 /* label between threads */;
      reached_end_of_stack = true;
      break;
    }

    stack_frames.emplace_back(CreateStackFrame(context, frame, frame_format));
  }

  if (include_all && reached_end_of_stack) {
    for (uint32_t bt = 0; bt < thread.GetProcess().GetNumExtendedBacktraceTypes(); bt++) {
      lldb::SBThread backtrace = thread.GetExtendedBacktraceThread(thread.GetProcess().GetExtendedBacktraceTypeAtIndex(bt));
      if (!backtrace.IsValid())
        continue;

      reached_end_of_stack =
          FillStackFrames(context, backtrace, frame_format, stack_frames, offset,
                          (start_frame - offset) > 0 ? start_frame - offset : -1, levels, include_all);
      if (static_cast<int64_t>(stack_frames.size()) >= levels)
        break;
    }
  }

  return reached_end_of_stack;
}

}  // namespace

protocol::Scope CreateScope(ScopeKind kind, int64_t variablesReference, int64_t namedVariables,
                            bool expensive) {
  protocol::Scope scope;
  scope.variablesReference = variablesReference;
  scope.namedVariables = namedVariables;
  scope.expensive = expensive;

  // TODO: Support "arguments" and "return value" scope.
  // VS Code only expands the first non-expensive scope. This causes friction
  // if we add the arguments above the local scope, as the locals scope will not
  // be expanded if we enter a function with arguments. It becomes more
  // annoying when the scope has arguments, return_value and locals.
  switch (kind) {
  case eScopeKindLocals:
    scope.presentationHint = protocol::Scope::eScopePresentationHintLocals;
    scope.name = "Locals";
    break;
  case eScopeKindGlobals:
    scope.name = "Globals";
    break;
  case eScopeKindRegisters:
    scope.presentationHint = protocol::Scope::eScopePresentationHintRegisters;
    scope.name = "Registers";
    break;
  }

  return scope;
}

std::optional<ScopeData> Variables::GetTopLevelScope(int64_t variablesReference) {
  auto scope_kind_iter = m_scope_kinds.find(variablesReference);
  if (scope_kind_iter == m_scope_kinds.end())
    return std::nullopt;

  ScopeKind scope_kind = scope_kind_iter->second.first;
  uint64_t dap_frame_id = scope_kind_iter->second.second;

  auto frame_iter = m_frames.find(dap_frame_id);
  if (frame_iter == m_frames.end())
    return std::nullopt;

  lldb::SBValueList *scope = frame_iter->second.GetScope(scope_kind);
  if (scope == nullptr)
    return std::nullopt;

  ScopeData scope_data;
  scope_data.kind = scope_kind;
  scope_data.scope = *scope;
  return scope_data;
}

void Variables::Clear() {
  m_referencedvariables.clear();
  m_scope_kinds.clear();
  m_frames.clear();
  m_next_temporary_var_ref = TemporaryVariableStartIndex;
}

int64_t Variables::GetNewVariableReference(bool is_permanent) {
  if (is_permanent)
    return m_next_permanent_var_ref++;
  return m_next_temporary_var_ref++;
}

bool Variables::IsPermanentVariableReference(int64_t var_ref) {
  return var_ref >= PermanentVariableStartIndex;
}

lldb::SBValue Variables::GetVariable(int64_t var_ref) const {
  if (IsPermanentVariableReference(var_ref)) {
    auto pos = m_referencedpermanent_variables.find(var_ref);
    if (pos != m_referencedpermanent_variables.end())
      return pos->second;
  } else {
    auto pos = m_referencedvariables.find(var_ref);
    if (pos != m_referencedvariables.end())
      return pos->second;
  }
  return lldb::SBValue();
}

int64_t Variables::InsertVariable(lldb::SBValue variable, bool is_permanent) {
  int64_t var_ref = GetNewVariableReference(is_permanent);
  if (is_permanent)
    m_referencedpermanent_variables.insert(std::make_pair(var_ref, variable));
  else
    m_referencedvariables.insert(std::make_pair(var_ref, variable));
  return var_ref;
}

lldb::SBValue Variables::FindVariable(uint64_t variablesReference, llvm::StringRef name) {
  lldb::SBValue variable;
  if (std::optional<ScopeData> scope_data = GetTopLevelScope(variablesReference)) {
    bool is_duplicated_variable_name = name.contains(" @");
    // variablesReference is one of our scopes, not an actual variable.
    int64_t end_idx = scope_data->scope.GetSize();
    for (int64_t i = end_idx - 1; i >= 0; --i) {
      lldb::SBValue curr_variable = scope_data->scope.GetValueAtIndex(i);
      std::string variable_name = CreateUniqueVariableNameForDisplay(curr_variable, is_duplicated_variable_name);
      if (variable_name == name) {
        variable = curr_variable;
        break;
      }
    }
  } else {
    lldb::SBValue container = GetVariable(variablesReference);
    variable = container.GetChildMemberWithName(name.data());
    if (!variable.IsValid()) {
      if (name.starts_with("[")) {
        llvm::StringRef index_str(name.drop_front(1));
        uint64_t index = 0;
        if (!index_str.consumeInteger(0, index)) {
          if (index_str == "]")
            variable = container.GetChildAtIndex(index);
        }
      }
    }
  }
  return variable;
}

lldb::SBValueList *Variables::GetScope(uint64_t dap_frame_id, ScopeKind kind) {
  auto frame = m_frames.find(dap_frame_id);
  if (frame == m_frames.end())
    return nullptr;

  return frame->second.GetScope(kind);
}

std::vector<protocol::Scope> Variables::CreateScopes(uint64_t dap_frame_id, lldb::SBFrame &frame) {
  auto iter = m_frames.find(dap_frame_id);
  if (iter == m_frames.end()) {
    auto locals = frame.GetVariables(/*arguments=*/true,
                                     /*locals=*/true,
                                     /*statics=*/false,
                                     /*in_scope_only=*/true);

    auto globals = frame.GetVariables(/*arguments=*/false,
                                      /*locals=*/false,
                                      /*statics=*/true,
                                      /*in_scope_only=*/true);

    auto registers = frame.GetRegisters();

    iter = m_frames.emplace(dap_frame_id, FrameScopes{locals, globals, registers}).first;
  }

  const FrameScopes &frame_scopes = iter->second;

  auto create_scope = [&](ScopeKind kind, uint32_t size) {
    int64_t ref = GetNewVariableReference(false);
    m_scope_kinds.try_emplace(ref, kind, dap_frame_id);
    return CreateScope(kind, ref, size, false);
  };

  return {
      create_scope(eScopeKindLocals, frame_scopes.locals.GetSize()),
      create_scope(eScopeKindGlobals, frame_scopes.globals.GetSize()),
      create_scope(eScopeKindRegisters, frame_scopes.registers.GetSize()),
  };
}

ReplMode DataManager::DetectReplMode(lldb::SBFrame &frame, std::string &expression, bool partial_expression) {
  if (llvm::StringRef expr_ref = expression; expr_ref.consume_front(m_context.CommandEscapePrefix())) {
    expression = expr_ref;
    return ReplMode::Command;
  }

  if (repl_mode != ReplMode::Auto)
    return repl_mode;

  const auto [first_tok, remaining] = llvm::getToken(expression);
  if (partial_expression && remaining.empty())
    return ReplMode::Auto;

  std::string first = first_tok.str();
  const char *first_cstr = first.c_str();
  lldb::SBCommandInterpreter interpreter = m_context.Lldb().debugger.GetCommandInterpreter();
  const bool is_command = interpreter.CommandExists(first_cstr) || interpreter.UserCommandExists(first_cstr) ||
                          interpreter.AliasExists(first_cstr);
  const bool is_variable = frame.FindVariable(first_cstr).IsValid();

  if (!partial_expression && is_command && is_variable) {
    const std::string warning_msg =
        llvm::formatv("warning: Expression '{}' is both an LLDB command and "
                      "variable. It will be evaluated as a variable. To evaluate "
                      "the expression as an LLDB command, use '{}' as a prefix.\n",
                      first, m_context.CommandEscapePrefix())
            .str();
    m_context.SendOutput(OutputCategory::Console, warning_msg);
  }

  if (is_variable)
    return ReplMode::Variable;
  return is_command ? ReplMode::Command : ReplMode::Variable;
}

void DataManager::SetFrameFormat(llvm::StringRef format) {
  lldb::SBError error;
  frame_format = lldb::SBFormat(format.str().c_str(), error);
  if (error.Fail()) {
    m_context.SendOutput(
        OutputCategory::Console,
        llvm::formatv("The provided frame format '{0}' couldn't be parsed: {1}\n", format, error.GetCString())
            .str());
  }
}

void DataManager::SetThreadFormat(llvm::StringRef format) {
  lldb::SBError error;
  thread_format = lldb::SBFormat(format.str().c_str(), error);
  if (error.Fail()) {
    m_context.SendOutput(
        OutputCategory::Console,
        llvm::formatv("The provided thread format '{0}' couldn't be parsed: {1}\n", format, error.GetCString())
            .str());
  }
}

llvm::Expected<protocol::StackTraceResponseBody>
DataManager::GetStackTraceRequest(const protocol::StackTraceArguments &args) {
  return m_context.WithTarget([&]() -> llvm::Expected<protocol::StackTraceResponseBody> {
    lldb::SBThread thread = m_context.GetLLDBThread(args.threadId);
    if (!thread.IsValid())
      return llvm::make_error<dap::DAPError>("invalid thread");

    lldb::SBFormat format = frame_format;
    bool include_all = m_context.Session().configuration.displayExtendedBacktrace;

    if (args.format) {
      const protocol::StackFrameFormat &requested = *args.format;
      include_all = requested.includeAll;

      // FIXME: Support "parameterTypes" and "hex".
      if (requested.module || requested.line || requested.parameters || requested.parameterNames ||
          requested.parameterValues) {
        std::string format_str;
        llvm::raw_string_ostream os(format_str);

        if (requested.module)
          os << "{${module.file.basename} }";
        if (requested.line)
          os << "{${line.file.basename}:${line.number}:${line.column} }";
        if (requested.parameters || requested.parameterNames || requested.parameterValues)
          os << "{${function.name-with-args}}";
        else
          os << "{${function.name-without-args}}";

        lldb::SBError error;
        format = lldb::SBFormat(format_str.c_str(), error);
        if (error.Fail())
          return ToError(error);
      }
    }

    protocol::StackTraceResponseBody body;
    const int64_t levels = args.levels == 0 ? INT64_MAX : args.levels;
    int64_t offset = 0;
    bool reached_end_of_stack =
        FillStackFrames(m_context, thread, format, body.stackFrames, offset, args.startFrame, levels, include_all);
    body.totalFrames = args.startFrame + body.stackFrames.size() + (reached_end_of_stack ? 0 : kStackPageSize);

    return body;
  });
}

protocol::ScopesResponseBody DataManager::GetScopesRequest(const protocol::ScopesArguments &args) {
  return m_context.WithTarget([&]() -> protocol::ScopesResponseBody {
    lldb::SBFrame frame = m_context.GetLLDBFrame(args.frameId);

    // Selects the frame's thread so LLDB-console commands stay scoped to
    // whatever frame the client is currently viewing - there's no other event
    // that tells the GUI (or us) which thread/frame is selected.
    if (frame.IsValid()) {
      frame.GetThread().GetProcess().SetSelectedThread(frame.GetThread());
      frame.GetThread().SetSelectedFrame(frame.GetFrameID());
    }

    return protocol::ScopesResponseBody{variables.CreateScopes(args.frameId, frame)};
  });
}

protocol::VariablesResponseBody DataManager::GetVariablesRequest(const protocol::VariablesArguments &args) {
  return m_context.WithTarget([&]() -> protocol::VariablesResponseBody {
    const uint64_t var_ref = args.variablesReference;
    const uint64_t count = args.count;
    const uint64_t start = args.start;
    const bool hex = args.format ? args.format->hex : false;
    const bool auto_summaries = m_context.Session().configuration.enableAutoVariableSummaries;
    const bool synthetic_child_debugging = m_context.Session().configuration.enableSyntheticChildDebugging;

    std::vector<protocol::Variable> result;

    std::optional<ScopeData> scope_data = variables.GetTopLevelScope(var_ref);
    if (scope_data) {
      int64_t start_idx = 0;
      int64_t num_children = 0;

      if (scope_data->kind == eScopeKindRegisters) {
        const uint32_t addr_size = m_context.Target().GetProcess().GetAddressByteSize();
        lldb::SBValue reg_set = scope_data->scope.GetValueAtIndex(0);
        const uint32_t num_regs = reg_set.GetNumChildren();
        for (uint32_t reg_idx = 0; reg_idx < num_regs; ++reg_idx) {
          lldb::SBValue reg = reg_set.GetChildAtIndex(reg_idx);
          const lldb::Format format = reg.GetFormat();
          if (format == lldb::eFormatDefault || format == lldb::eFormatHex) {
            if (reg.GetByteSize() == addr_size)
              reg.SetFormat(lldb::eFormatAddressInfo);
          }
        }
      }

      num_children = scope_data->scope.GetSize();
      if (num_children == 0 && scope_data->kind == eScopeKindLocals) {
        lldb::SBError error = scope_data->scope.GetError();
        if (const char *var_err = error.GetCString()) {
          protocol::Variable var;
          var.name = "<error>";
          var.type = "const char *";
          var.value = var_err;
          result.emplace_back(var);
        }
      }
      const int64_t end_idx = start_idx + ((count == 0) ? num_children : count);

      std::map<llvm::StringRef, int> variable_name_counts;
      for (auto i = start_idx; i < end_idx; ++i) {
        lldb::SBValue variable = scope_data->scope.GetValueAtIndex(i);
        if (!variable.IsValid())
          break;
        variable_name_counts[GetNonNullVariableName(variable)]++;
      }

      if (scope_data->kind == eScopeKindLocals) {
        lldb::SBProcess process = m_context.Target().GetProcess();
        lldb::SBThread selected_thread = process.GetSelectedThread();
        lldb::SBValue stop_return_value = selected_thread.GetStopReturnValue();

        if (stop_return_value.IsValid() && selected_thread.GetSelectedFrame().GetFrameID() == 0) {
          lldb::SBValue renamed_return_value = stop_return_value.Clone("(Return Value)");
          int64_t return_var_ref = 0;

          if (stop_return_value.MightHaveChildren() || stop_return_value.IsSynthetic())
            return_var_ref = variables.InsertVariable(stop_return_value, /*is_permanent=*/false);

          result.emplace_back(CreateVariable(renamed_return_value, return_var_ref, hex, auto_summaries,
                                             synthetic_child_debugging, false));
        }
      }

      for (auto i = start_idx; i < end_idx; ++i) {
        lldb::SBValue variable = scope_data->scope.GetValueAtIndex(i);
        if (!variable.IsValid())
          break;

        const int64_t frame_var_ref = variables.InsertVariable(variable, /*is_permanent=*/false);
        result.emplace_back(CreateVariable(variable, frame_var_ref, hex, auto_summaries, synthetic_child_debugging,
                                           variable_name_counts[GetNonNullVariableName(variable)] > 1));
      }
    } else {
      // Expanding a variable that has children - return them.
      lldb::SBValue variable = variables.GetVariable(var_ref);
      if (variable.IsValid()) {
        const bool is_permanent = variables.IsPermanentVariableReference(var_ref);
        auto add_child = [&](lldb::SBValue child, std::optional<llvm::StringRef> custom_name = {},
                             std::optional<uint64_t> field_offset = std::nullopt) {
          if (!child.IsValid())
            return;
          const int64_t child_var_ref = variables.InsertVariable(child, is_permanent);
          result.emplace_back(CreateVariable(child, child_var_ref, hex, auto_summaries, synthetic_child_debugging,
                                             /*is_name_duplicated=*/false, custom_name, field_offset));
        };
        const int64_t num_children = variable.GetNumChildren();
        const int64_t end_idx = start + ((count == 0) ? num_children : count);
        int64_t i = start;
        for (; i < end_idx && i < num_children; ++i) {
          // Only a genuine struct/union field has a matching SBTypeMember -
          // GetFieldAtIndex() is invalid for array elements and synthetic
          // children, which naturally excludes them from getting an offset.
          lldb::SBTypeMember member = variable.GetType().GetFieldAtIndex(static_cast<uint32_t>(i));
          add_child(variable.GetChildAtIndex(i), /*custom_name=*/{},
                    member.IsValid() ? std::optional<uint64_t>(member.GetOffsetInBytes()) : std::nullopt);
        }

        if (synthetic_child_debugging && variable.IsSynthetic() && i == num_children)
          add_child(variable.GetNonSyntheticValue(), "[raw]");
      }
    }

    return protocol::VariablesResponseBody{std::move(result)};
  });
}

llvm::Expected<protocol::SetVariableResponseBody>
DataManager::SetVariableRequest(const protocol::SetVariableArguments &args) {
  return m_context.WithTarget([&]() -> llvm::Expected<protocol::SetVariableResponseBody> {
    const llvm::StringRef args_name = args.name;

    if (args.variablesReference == UINT64_MAX)
      return llvm::make_error<dap::DAPError>(llvm::formatv("invalid reference {}", args.variablesReference).str(),
                                             llvm::inconvertibleErrorCode(), /*show_user=*/false);

    constexpr llvm::StringRef return_value_name = "(Return Value)";
    if (args_name == return_value_name)
      return llvm::make_error<dap::DAPError>("cannot change the value of the return value");

    lldb::SBValue variable = variables.FindVariable(args.variablesReference, args_name);
    if (!variable.IsValid())
      return llvm::make_error<dap::DAPError>("could not find variable in scope");

    lldb::SBError error;
    if (!variable.SetValueFromCString(args.value.c_str(), error))
      return llvm::make_error<dap::DAPError>(error.GetCString());

    VariableDescription desc(variable, m_context.Session().configuration.enableAutoVariableSummaries);

    protocol::SetVariableResponseBody body;
    body.value = desc.display_value;
    body.type = desc.display_type_name;

    const int64_t new_var_ref = variables.InsertVariable(variable, /*is_permanent=*/false);
    if (variable.MightHaveChildren()) {
      body.variablesReference = new_var_ref;
      if (desc.type_obj.IsArrayType())
        body.indexedVariables = variable.GetNumChildren();
      else
        body.namedVariables = variable.GetNumChildren();
    }

    if (const lldb::addr_t addr = GetRealLoadAddress(variable); addr != LLDB_INVALID_ADDRESS)
      body.memoryReference = addr;

    if (ValuePointsToCode(variable))
      body.valueLocationReference = new_var_ref;

    m_context.Execution().SendInvalidatedEvent({protocol::InvalidatedEventBody::eAreaVariables});
    m_context.Execution().SendMemoryEvent(variable);

    return body;
  });
}

llvm::Expected<protocol::EvaluateResponseBody>
DataManager::GetEvaluateRequest(const protocol::EvaluateArguments &args) {
  return m_context.WithTarget([&]() -> llvm::Expected<protocol::EvaluateResponseBody> {
    protocol::EvaluateResponseBody body;
    lldb::SBFrame frame = m_context.GetLLDBFrame(args.frameId);
    std::string expression = args.expression;
    bool repeat_last_command = expression.empty() && last_nonempty_var_expression.empty();

    if (args.context == protocol::eEvaluateContextRepl &&
        (repeat_last_command || (!expression.empty() && DetectReplMode(frame, expression, false) == ReplMode::Command))) {
      // Not a variable expression - clear the repeat-last-variable cache.
      last_nonempty_var_expression.clear();
      if (frame.IsValid())
        m_context.Execution().focus_tid = frame.GetThread().GetThreadID();

      bool required_command_failed = false;
      body.result = RunLLDBCommands(m_context.Lldb().debugger, llvm::StringRef(), {expression}, required_command_failed,
                                    /*parse_command_directives=*/false, /*echo_commands=*/false);
      return body;
    }

    if (args.context == protocol::eEvaluateContextRepl) {
      if (expression.empty())
        expression = last_nonempty_var_expression;
      else
        last_nonempty_var_expression = expression;
    }

    lldb::SBValue value = frame.GetValueForVariablePath(expression.data(), lldb::eDynamicDontRunTarget);
    const bool resolved_as_variable_path = value.GetError().Success();
    // Persist() (below) relocates the value into LLDB's own
    // persistent-expression bookkeeping ($0, $1, ...) - on a bare-metal
    // target with no process/allocator backing that bookkeeping, the
    // persisted copy's own GetLoadAddress() can point into unrelated
    // memory instead of the original variable's storage. Kept around so the
    // address computation below can use the real variable instead.
    lldb::SBValue variable_path_value = value;

    if (resolved_as_variable_path && args.context == protocol::eEvaluateContextRepl)
      value = value.Persist();

    if (value.GetError().Fail() && args.context != protocol::eEvaluateContextHover)
      value = frame.EvaluateExpression(expression.data());

    if (value.GetError().Fail())
      return ToError(value.GetError(), /*show_user=*/false);

    const bool hex = args.format ? args.format->hex : false;
    VariableDescription desc(value, m_context.Session().configuration.enableAutoVariableSummaries, hex);

    body.result = desc.GetResult(args.context);
    body.type = desc.display_type_name;

    if (value.MightHaveChildren() || ValuePointsToCode(value))
      body.variablesReference = variables.InsertVariable(value, /*is_permanent=*/args.context == protocol::eEvaluateContextRepl);

    lldb::SBValue &address_source = resolved_as_variable_path ? variable_path_value : value;
    const bool is_pointer = address_source.GetType().IsPointerType();
    const lldb::addr_t addr = GetRealLoadAddress(address_source);

    if (addr != LLDB_INVALID_ADDRESS)
      body.memoryReference = dap::EncodeMemoryReference(addr);

    // VS Code's hover tooltip only renders `result`, not `memoryReference` -
    // fold the address into the displayed text so hovering a variable shows
    // both. Skipped for pointers: they already display the pointee value,
    // and showing the pointer's own address on top of that reads as if the
    // value were doubly indirected.
    if (args.context == protocol::eEvaluateContextHover && addr != LLDB_INVALID_ADDRESS && !is_pointer)
      body.result += " @ " + dap::EncodeMemoryReference(addr);

    if (ValuePointsToCode(value) && body.variablesReference != LLDB_DAP_INVALID_VAR_REF)
      body.valueLocationReference = dap::PackLocation(body.variablesReference, true);

    return body;
  });
}

namespace {

size_t GetLineStartPos(llvm::StringRef text, uint32_t line) {
  if (line == 0)
    return llvm::StringRef::npos;
  if (line == 1)
    return 0;

  uint32_t cur_line = 1;
  size_t pos = 0;
  while (cur_line < line) {
    const size_t new_line_pos = text.find('\n', pos);
    if (new_line_pos == llvm::StringRef::npos)
      return new_line_pos;

    pos = new_line_pos + 1;
    if (pos >= text.size())
      return llvm::StringRef::npos;

    cur_line++;
  }

  return pos;
}

std::optional<size_t> GetCursorPos(llvm::StringRef text, uint32_t line, uint32_t utf16_codeunits) {
  if (text.empty())
    return std::nullopt;

  const size_t line_start_pos = GetLineStartPos(text, line);
  if (line_start_pos == llvm::StringRef::npos)
    return std::nullopt;

  const llvm::StringRef completion_line = text.substr(line_start_pos, text.find('\n', line_start_pos));
  if (completion_line.empty())
    return std::nullopt;

  const std::optional<size_t> cursor_pos_opt = dap::UTF16CodeunitToBytes(completion_line, utf16_codeunits);
  if (!cursor_pos_opt)
    return std::nullopt;

  return line_start_pos + *cursor_pos_opt;
}

size_t GetPartialTokenCodeUnits(llvm::StringRef line, size_t cursor_pos) {
  line = line.substr(0, cursor_pos);
  const size_t idx = line.rfind(' ');

  const llvm::StringRef byte_token = line.substr(idx == llvm::StringRef::npos ? 0 : idx + 1);
  llvm::SmallVector<llvm::UTF16, 20> utf16_token;
  if (llvm::convertUTF8ToUTF16String(byte_token, utf16_token))
    return utf16_token.size();
  return byte_token.size();
}

}  // namespace

protocol::CompletionsResponseBody DataManager::GetCompletionsRequest(const protocol::CompletionsArguments &args) {
  return m_context.WithTarget([&]() -> protocol::CompletionsResponseBody {
    std::string text = args.text;
    const uint32_t line = args.line;
    const uint32_t utf16_codeunits = args.column - 1;

    const auto cursor_pos_opt = GetCursorPos(text, line, utf16_codeunits);
    if (!cursor_pos_opt)
      return protocol::CompletionsResponseBody{};

    size_t cursor_pos = *cursor_pos_opt;

    lldb::SBFrame frame = m_context.GetLLDBFrame(args.frameId);
    if (frame.IsValid()) {
      lldb::SBThread frame_thread = frame.GetThread();
      frame_thread.GetProcess().SetSelectedThread(frame_thread);
      frame_thread.SetSelectedFrame(frame.GetFrameID());
    }

    const llvm::StringRef escape_prefix = m_context.Session().configuration.commandEscapePrefix;
    const bool had_escape_prefix = llvm::StringRef(text).starts_with(escape_prefix);
    const ReplMode mode = DetectReplMode(frame, text, true);
    if (had_escape_prefix) {
      if (cursor_pos < escape_prefix.size())
        return protocol::CompletionsResponseBody{};
      cursor_pos -= escape_prefix.size();
    }

    const size_t partial_token_cu = GetPartialTokenCodeUnits(text, cursor_pos);
    const std::string expr_prefix = "expression -- ";
    const std::array<std::tuple<ReplMode, std::string, uint64_t>, 2> exprs = {
        {std::make_tuple(ReplMode::Command, text, cursor_pos),
         std::make_tuple(ReplMode::Variable, expr_prefix + text, cursor_pos + expr_prefix.size())}};

    protocol::CompletionsResponseBody response;
    lldb::SBCommandInterpreter interpreter = m_context.Lldb().debugger.GetCommandInterpreter();
    for (const auto &[expr_mode, expr_line, expr_cursor] : exprs) {
      if (mode != ReplMode::Auto && mode != expr_mode)
        continue;

      lldb::SBStringList matches;
      lldb::SBStringList descriptions;
      if (!interpreter.HandleCompletionWithDescriptions(expr_line.c_str(), expr_cursor, 0, 50, matches, descriptions))
        continue;

      for (uint32_t i = 1; i < matches.GetSize(); i++) {
        const llvm::StringRef match = matches.GetStringAtIndex(i);
        const llvm::StringRef description = descriptions.GetStringAtIndex(i);

        protocol::CompletionItem item;
        item.label = match;
        if (!description.empty())
          item.detail = description;
        item.length = partial_token_cu; // Overwrite lldb's own partial-token length.

        response.targets.emplace_back(std::move(item));
      }
    }

    return response;
  });
}

llvm::Expected<protocol::LocationsResponseBody>
DataManager::GetLocationsRequest(const protocol::LocationsArguments &args) {
  return m_context.WithTarget([&]() -> llvm::Expected<protocol::LocationsResponseBody> {
    protocol::LocationsResponseBody response;
    auto [var_ref, is_value_location] = dap::UnpackLocation(args.locationReference);
    lldb::SBValue variable = variables.GetVariable(var_ref);
    if (!variable.IsValid())
      return llvm::make_error<dap::DAPError>("Invalid variable reference");

    if (is_value_location) {
      if (!variable.GetType().IsPointerType() && !variable.GetType().IsReferenceType())
        return llvm::make_error<dap::DAPError>("Value locations are only available for pointers and references");

      lldb::addr_t raw_addr = variable.GetValueAsAddress();
      lldb::SBAddress addr = m_context.Target().ResolveLoadAddress(raw_addr);
      lldb::SBLineEntry line_entry = GetLineEntryForAddress(m_context.Target(), addr);
      if (!line_entry.IsValid())
        return llvm::make_error<dap::DAPError>("Failed to resolve line entry for location");

      std::optional<protocol::Source> source = CreateSource(line_entry.GetFileSpec());
      if (!source)
        return llvm::make_error<dap::DAPError>("Failed to resolve file path for location");

      response.source = std::move(*source);
      response.line = line_entry.GetLine();
      response.column = line_entry.GetColumn();
    } else {
      lldb::SBDeclaration decl = variable.GetDeclaration();
      if (!decl.IsValid())
        return llvm::make_error<dap::DAPError>("No declaration location available");

      std::optional<protocol::Source> source = CreateSource(decl.GetFileSpec());
      if (!source)
        return llvm::make_error<dap::DAPError>("Failed to resolve file path for location");

      response.source = std::move(*source);
      response.line = decl.GetLine();
      response.column = decl.GetColumn();
    }

    return response;
  });
}

protocol::Variable CreateVariable(lldb::SBValue v, int64_t var_ref, bool format_hex, bool auto_variable_summaries,
                                  bool synthetic_child_debugging, bool is_name_duplicated,
                                  std::optional<llvm::StringRef> custom_name,
                                  std::optional<uint64_t> field_offset) {
  VariableDescription desc(v, auto_variable_summaries, format_hex, is_name_duplicated, custom_name);
  protocol::Variable var;
  var.name = desc.name;
  var.value = desc.display_value;
  var.type = desc.display_type_name;

  if (!desc.evaluate_name.empty())
    var.evaluateName = desc.evaluate_name;

  if (desc.type_obj.IsArrayType()) {
    var.indexedVariables = v.GetNumChildren();
  } else if (v.IsSynthetic()) {
    if (lldb::SBValue first_child = v.GetChildAtIndex(0)) {
      llvm::StringRef first_child_name = first_child.GetName();
      if (first_child_name == "[0]") {
        size_t num_children = v.GetNumChildren();
        if (synthetic_child_debugging)
          ++num_children;
        var.indexedVariables = num_children;
      }
    }
  }

  if (v.MightHaveChildren())
    var.variablesReference = var_ref;

  if (v.GetDeclaration().IsValid())
    var.declarationLocationReference = dap::PackLocation(var_ref, false);

  if (ValuePointsToCode(v))
    var.valueLocationReference = dap::PackLocation(var_ref, true);

  const lldb::addr_t addr = GetRealLoadAddress(v);
  if (addr != LLDB_INVALID_ADDRESS) {
    var.memoryReference = addr;
    var.byteSize = v.GetByteSize();
  }

  // Same as the hover tooltip (DataManager::GetEvaluateRequest): fold the
  // address into the displayed text, since VS Code doesn't render
  // memoryReference anywhere on its own. The offset (this field's byte
  // position within its immediate parent struct) is independently useful -
  // e.g. for comparing against a datasheet register map - so it's shown
  // even for pointer fields, which otherwise skip the address (they
  // already display the pointee value; appending the pointer's own
  // address on top would read as double indirection).
  if (field_offset || (addr != LLDB_INVALID_ADDRESS && !v.GetType().IsPointerType())) {
    std::string suffix;
    if (field_offset)
      suffix += llvm::formatv(" (+{0:x})", *field_offset).str();
    if (addr != LLDB_INVALID_ADDRESS && !v.GetType().IsPointerType())
      suffix += " @ " + dap::EncodeMemoryReference(addr);
    var.value += suffix;
  }

  bool is_readonly = v.GetType().IsAggregateType() || v.GetValueType() == lldb::eValueTypeRegisterSet;
  if (is_readonly) {
    if (!var.presentationHint)
      var.presentationHint = {protocol::VariablePresentationHint()};
    var.presentationHint->attributes.push_back("readOnly");
  }

  return var;
}

}  // namespace core
