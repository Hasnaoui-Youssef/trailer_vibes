#include "core/components/data_manager.hpp"

#include "core/debug_context.hpp"
#include "core/variable_description.hpp"
#include "dap/json_utils.hpp"
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBDeclaration.h"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBType.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FormatVariadic.h"

namespace core {

protocol::Scope CreateScope(ScopeKind kind, int64_t variablesReference, int64_t namedVariables,
                            bool expensive) {
  protocol::Scope scope;
  scope.variablesReference = variablesReference;
  scope.namedVariables = namedVariables;
  scope.expensive = expensive;

  // TODO: Support "arguments" and "return value" scope.
  // At the moment lldb-dap includes the arguments and return_value into the
  // "locals" scope.
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
    // variablesReference is one of our scopes, not an actual variable it is
    // asking for a variable in locals or globals or registers
    int64_t end_idx = scope_data->scope.GetSize();
    // Searching backward so that we choose the variable in closest scope
    // among variables of the same name.
    for (int64_t i = end_idx - 1; i >= 0; --i) {
      lldb::SBValue curr_variable = scope_data->scope.GetValueAtIndex(i);
      std::string variable_name = CreateUniqueVariableNameForDisplay(curr_variable, is_duplicated_variable_name);
      if (variable_name == name) {
        variable = curr_variable;
        break;
      }
    }
  } else {
    // This is not under the globals or locals scope, so there are no
    // duplicated names.

    // We have a named item within an actual variable so we need to find it
    // withing the container variable by name.
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

protocol::Variable CreateVariable(lldb::SBValue v, int64_t var_ref, bool format_hex, bool auto_variable_summaries,
                                  bool synthetic_child_debugging, bool is_name_duplicated,
                                  std::optional<llvm::StringRef> custom_name) {
  VariableDescription desc(v, auto_variable_summaries, format_hex, is_name_duplicated, custom_name);
  protocol::Variable var;
  var.name = desc.name;
  var.value = desc.display_value;
  var.type = desc.display_type_name;

  if (!desc.evaluate_name.empty())
    var.evaluateName = desc.evaluate_name;

  // If we have a type with many children, we would like to be able to give a
  // hint to the IDE that the type has indexed children so that the request
  // can be broken up in grabbing only a few children at a time. We want to
  // be careful and only call "v.GetNumChildren()" if we have an array type
  // or if we have a synthetic child provider producing indexed children. We
  // don't want to call "v.GetNumChildren()" on all objects as class, struct
  // and union types don't need to be completed if they are never expanded.
  // So we want to avoid calling this to only cases where it makes sense to
  // keep performance high during normal debugging.

  // If we have an array type, say that it is indexed and provide the number
  // of children in case we have a huge array. If we don't do this, then we
  // might take a while to produce all children at once which can delay your
  // debug session.
  if (desc.type_obj.IsArrayType()) {
    var.indexedVariables = v.GetNumChildren();
  } else if (v.IsSynthetic()) {
    // For a type with a synthetic child provider, the SBType of "v" won't
    // tell us anything about what might be displayed. Instead, we check if
    // the first child's name is "[0]" and then say it is indexed. We call
    // GetNumChildren() only if the child name matches to avoid a
    // potentially expensive operation.
    if (lldb::SBValue first_child = v.GetChildAtIndex(0)) {
      llvm::StringRef first_child_name = first_child.GetName();
      if (first_child_name == "[0]") {
        size_t num_children = v.GetNumChildren();
        // If we are creating a "[raw]" fake child for each synthetic type,
        // we have to account for it when returning indexed variables.
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

  if (lldb::addr_t addr = v.GetLoadAddress(); addr != LLDB_INVALID_ADDRESS)
    var.memoryReference = addr;

  bool is_readonly = v.GetType().IsAggregateType() || v.GetValueType() == lldb::eValueTypeRegisterSet;
  if (is_readonly) {
    if (!var.presentationHint)
      var.presentationHint = {protocol::VariablePresentationHint()};
    var.presentationHint->attributes.push_back("readOnly");
  }

  return var;
}

}  // namespace core
