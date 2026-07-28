//===-- ProtocolTypes.h ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains POD structs based on the DAP specification at
// https://microsoft.github.io/debug-adapter-protocol/specification
//
// This is not meant to be a complete implementation, new interfaces are added
// when they're needed.
//
// Each struct requires a toJSON and fromJSON function
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_DAP_PROTOCOL_PROTOCOL_REQUESTS_HPP_
#define TRAILER_DAP_PROTOCOL_PROTOCOL_REQUESTS_HPP_

#include "dap/protocol/protocol_base.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "dap/protocol/dap_defines.hpp"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/JSON.h"
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace dap::protocol {

struct CancelArguments {
  std::optional<int64_t> requestId;
  std::optional<int64_t> progressId;
};

bool fromJSON(const llvm::json::Value &, CancelArguments &, llvm::json::Path);

using CancelResponse = VoidResponse;

struct DisconnectArguments {
  std::optional<bool> restart;
  std::optional<bool> terminateDebuggee;
  std::optional<bool> suspendDebuggee;
};
bool fromJSON(const llvm::json::Value &, DisconnectArguments &,
              llvm::json::Path);

using DisconnectResponse = VoidResponse;

enum ClientFeature : unsigned {
  eClientFeatureVariableType,
  eClientFeatureVariablePaging,
  eClientFeatureRunInTerminalRequest,
  eClientFeatureMemoryReferences,
  eClientFeatureProgressReporting,
  eClientFeatureInvalidatedEvent,
  eClientFeatureMemoryEvent,
  eClientFeatureArgsCanBeInterpretedByShell,
  eClientFeatureStartDebuggingRequest,
  eClientFeatureANSIStyling,
};

enum PathFormat : unsigned { ePathFormatPath, ePathFormatURI };

struct InitializeRequestArguments {
  std::string adapterID;
  std::string clientID;
  std::string clientName;
  std::string locale;
  PathFormat pathFormat = ePathFormatPath;

  bool linesStartAt1 = true;
  bool columnsStartAt1 = true;

  llvm::DenseSet<ClientFeature> supportedFeatures;

  bool lldbExtSourceInitFile = true;
};
bool fromJSON(const llvm::json::Value &, InitializeRequestArguments &,
              llvm::json::Path);

using InitializeResponse = std::optional<Capabilities>;

struct Configuration {
  std::string debuggerRoot;

  bool enableAutoVariableSummaries = false;
  bool enableSyntheticChildDebugging = false;
  bool displayExtendedBacktrace = false;
  bool stopOnEntry = false;

  std::chrono::seconds timeout = std::chrono::seconds(30);
  std::string commandEscapePrefix = "`";
  std::optional<std::string> customFrameFormat;
  std::optional<std::string> customThreadFormat;

  std::string sourcePath;
  std::vector<std::pair<std::string, std::string>> sourceMap;

  std::vector<std::string> preInitCommands;
  std::vector<std::string> initCommands;
  std::vector<std::string> preRunCommands;
  std::vector<std::string> postRunCommands;
  std::vector<std::string> stopCommands;
  std::vector<std::string> exitCommands;
  std::vector<std::string> terminateCommands;

  std::string program;
  std::string targetTriple;
  std::string platformName;
};

struct OpenOcdConfiguration {
  std::vector<std::string> scriptSearchDirs;
  std::vector<std::string> configFiles;
  std::vector<std::string> rawCommands;
  std::string logFile = "openocd_logs.txt";
  int32_t debugLevel = 2;
  std::string gdbPort = "3333";
  std::string tclPort = "disabled";
  std::string telnetPort = "disabled";
};
bool fromJSON(const llvm::json::Value &, OpenOcdConfiguration &,
              llvm::json::Path);

struct LaunchRequestArguments {
  Configuration configuration;
  bool noDebug = false;

  std::vector<std::string> launchCommands;

  OpenOcdConfiguration openocd;
};
bool fromJSON(const llvm::json::Value &, LaunchRequestArguments &,
              llvm::json::Path);

using LaunchResponse = VoidResponse;

#define LLDB_DAP_INVALID_PORT (-1)
#define LLDB_DAP_INVALID_FRAME_ID UINT64_MAX

struct DAPSession {
  user_id_t targetId;
  user_id_t debuggerId;
};
bool fromJSON(const llvm::json::Value &, DAPSession &, llvm::json::Path);

struct AttachRequestArguments {
  Configuration configuration;

  std::vector<std::string> attachCommands;
  pid_t pid = LLDB_INVALID_PROCESS_ID;
  bool waitFor = false;
  int32_t gdbRemotePort = LLDB_DAP_INVALID_PORT;
  std::string gdbRemoteHostname = "localhost";
  std::string coreFile;
  std::optional<DAPSession> session;
};
bool fromJSON(const llvm::json::Value &, AttachRequestArguments &,
              llvm::json::Path);

using AttachResponse = VoidResponse;

struct ContinueArguments {
  tid_t threadId = LLDB_INVALID_THREAD_ID;
  bool singleThread = false;
};
bool fromJSON(const llvm::json::Value &, ContinueArguments &, llvm::json::Path);

struct ContinueResponseBody {
  bool allThreadsContinued = true;
};
llvm::json::Value toJSON(const ContinueResponseBody &);

struct CompletionsArguments {
  uint64_t frameId = LLDB_DAP_INVALID_FRAME_ID;
  std::string text;
  uint32_t column = LLDB_INVALID_COLUMN_NUMBER;
  uint32_t line = 1;
};
bool fromJSON(const llvm::json::Value &, CompletionsArguments &,
              llvm::json::Path);

struct CompletionsResponseBody {
  std::vector<CompletionItem> targets;
};
llvm::json::Value toJSON(const CompletionsResponseBody &);

using ConfigurationDoneArguments = EmptyArguments;
using ConfigurationDoneResponse = VoidResponse;

struct SetVariableArguments {
  uint64_t variablesReference = UINT64_MAX;
  std::string name;
  std::string value;
  ValueFormat format;
};
bool fromJSON(const llvm::json::Value &, SetVariableArguments &,
              llvm::json::Path);

struct SetVariableResponseBody {
  std::string value;
  std::string type;

  uint64_t variablesReference = 0;
  uint32_t namedVariables = 0;
  uint32_t indexedVariables = 0;
  addr_t memoryReference = LLDB_INVALID_ADDRESS;
  uint64_t valueLocationReference = 0;
};
llvm::json::Value toJSON(const SetVariableResponseBody &);

struct ScopesArguments {
  uint64_t frameId = LLDB_DAP_INVALID_FRAME_ID;
};
bool fromJSON(const llvm::json::Value &, ScopesArguments &, llvm::json::Path);

struct ScopesResponseBody {
  std::vector<Scope> scopes;
};
llvm::json::Value toJSON(const ScopesResponseBody &);

struct SourceArguments {
  std::optional<Source> source;

  int64_t sourceReference = LLDB_DAP_INVALID_SRC_REF;
};
bool fromJSON(const llvm::json::Value &, SourceArguments &, llvm::json::Path);
struct SourceResponseBody {
  std::string content;
  std::optional<std::string> mimeType;
};
llvm::json::Value toJSON(const SourceResponseBody &);

using ThreadsArguments = EmptyArguments;
struct ThreadsResponseBody {
  std::vector<Thread> threads;
};
llvm::json::Value toJSON(const ThreadsResponseBody &);

struct NextArguments {
  tid_t threadId = LLDB_INVALID_THREAD_ID;
  bool singleThread = false;
  SteppingGranularity granularity = eSteppingGranularityStatement;
};
bool fromJSON(const llvm::json::Value &, NextArguments &, llvm::json::Path);
using NextResponse = VoidResponse;

struct StepInArguments {
  tid_t threadId = LLDB_INVALID_THREAD_ID;
  bool singleThread = false;
  std::optional<uint64_t> targetId;
  SteppingGranularity granularity = eSteppingGranularityStatement;
};
bool fromJSON(const llvm::json::Value &, StepInArguments &, llvm::json::Path);
using StepInResponse = VoidResponse;

struct StepInTargetsArguments {
  uint64_t frameId = LLDB_DAP_INVALID_FRAME_ID;
};
bool fromJSON(const llvm::json::Value &, StepInTargetsArguments &,
              llvm::json::Path);

struct StepInTargetsResponseBody {
  std::vector<StepInTarget> targets;
};
llvm::json::Value toJSON(const StepInTargetsResponseBody &);

struct StepOutArguments {
  tid_t threadId = LLDB_INVALID_THREAD_ID;

  std::optional<bool> singleThread;

  SteppingGranularity granularity = eSteppingGranularityStatement;
};
bool fromJSON(const llvm::json::Value &, StepOutArguments &, llvm::json::Path);
using StepOutResponse = VoidResponse;

struct BreakpointLocationsArguments {
  Source source;

  uint32_t line;

  std::optional<uint32_t> column;

  std::optional<uint32_t> endLine;

  std::optional<uint32_t> endColumn;
};
bool fromJSON(const llvm::json::Value &, BreakpointLocationsArguments &,
              llvm::json::Path);

struct BreakpointLocationsResponseBody {
  std::vector<BreakpointLocation> breakpoints;
};
llvm::json::Value toJSON(const BreakpointLocationsResponseBody &);

struct SetBreakpointsArguments {
  Source source;

  std::optional<std::vector<SourceBreakpoint>> breakpoints;

  std::optional<std::vector<uint32_t>> lines;

  std::optional<bool> sourceModified;
};
bool fromJSON(const llvm::json::Value &, SetBreakpointsArguments &,
              llvm::json::Path);

struct SetBreakpointsResponseBody {
  std::vector<Breakpoint> breakpoints;
};
llvm::json::Value toJSON(const SetBreakpointsResponseBody &);

struct SetFunctionBreakpointsArguments {
  std::vector<FunctionBreakpoint> breakpoints;
};
bool fromJSON(const llvm::json::Value &, SetFunctionBreakpointsArguments &,
              llvm::json::Path);

struct SetFunctionBreakpointsResponseBody {
  std::vector<Breakpoint> breakpoints;
};
llvm::json::Value toJSON(const SetFunctionBreakpointsResponseBody &);

struct SetInstructionBreakpointsArguments {
  std::vector<InstructionBreakpoint> breakpoints;
};
bool fromJSON(const llvm::json::Value &, SetInstructionBreakpointsArguments &,
              llvm::json::Path);

struct SetInstructionBreakpointsResponseBody {
  std::vector<Breakpoint> breakpoints;
};
llvm::json::Value toJSON(const SetInstructionBreakpointsResponseBody &);

struct DataBreakpointInfoArguments {
  std::optional<int64_t> variablesReference;

  std::string name;

  uint64_t frameId = LLDB_DAP_INVALID_FRAME_ID;

  std::optional<int64_t> bytes;

  std::optional<bool> asAddress;

  std::optional<std::string> mode;
};
bool fromJSON(const llvm::json::Value &, DataBreakpointInfoArguments &,
              llvm::json::Path);

struct DataBreakpointInfoResponseBody {
  std::optional<std::string> dataId;

  std::string description;

  std::optional<std::vector<DataBreakpointAccessType>> accessTypes;

  std::optional<bool> canPersist;
};
llvm::json::Value toJSON(const DataBreakpointInfoResponseBody &);

struct SetDataBreakpointsArguments {
  std::vector<DataBreakpoint> breakpoints;
};
bool fromJSON(const llvm::json::Value &, SetDataBreakpointsArguments &,
              llvm::json::Path);

struct SetDataBreakpointsResponseBody {
  std::vector<Breakpoint> breakpoints;
};
llvm::json::Value toJSON(const SetDataBreakpointsResponseBody &);

struct SetExceptionBreakpointsArguments {
  std::vector<std::string> filters;

  std::vector<ExceptionFilterOptions> filterOptions;

  // unsupported keys: exceptionOptions
};
bool fromJSON(const llvm::json::Value &, SetExceptionBreakpointsArguments &,
              llvm::json::Path);

struct SetExceptionBreakpointsResponseBody {
  std::vector<Breakpoint> breakpoints;
};
llvm::json::Value toJSON(const SetExceptionBreakpointsResponseBody &);

struct DisassembleArguments {
  addr_t memoryReference = LLDB_INVALID_ADDRESS;

  int64_t offset = 0;

  int64_t instructionOffset = 0;

  uint32_t instructionCount = 0;

  bool resolveSymbols = false;
};
bool fromJSON(const llvm::json::Value &, DisassembleArguments &,
              llvm::json::Path);
llvm::json::Value toJSON(const DisassembleArguments &);

struct DisassembleResponseBody {
  std::vector<DisassembledInstruction> instructions;
};
bool fromJSON(const llvm::json::Value &, DisassembleResponseBody &,
              llvm::json::Path);
llvm::json::Value toJSON(const DisassembleResponseBody &);

struct ReadMemoryArguments {
  addr_t memoryReference = LLDB_INVALID_ADDRESS;

  int64_t offset = 0;

  uint64_t count = 0;
};
bool fromJSON(const llvm::json::Value &, ReadMemoryArguments &,
              llvm::json::Path);

struct ReadMemoryResponseBody {
  addr_t address = LLDB_INVALID_ADDRESS;

  uint64_t unreadableBytes = 0;

  std::vector<std::byte> data;
};
llvm::json::Value toJSON(const ReadMemoryResponseBody &);

struct ModulesArguments {
  uint32_t startModule = 0;

  uint32_t moduleCount = 0;
};
bool fromJSON(const llvm::json::Value &, ModulesArguments &, llvm::json::Path);

struct ModulesResponseBody {
  std::vector<Module> modules;

  uint32_t totalModules = 0;
};
llvm::json::Value toJSON(const ModulesResponseBody &);

struct VariablesArguments {
  uint64_t variablesReference;

  enum VariablesFilter : unsigned {
    eVariablesFilterBoth = 0,
    eVariablesFilterIndexed = 1 << 0,
    eVariablesFilterNamed = 1 << 1,
  };

  VariablesFilter filter = eVariablesFilterBoth;

  uint64_t start = 0;

  uint64_t count = 0;

  std::optional<ValueFormat> format;
};
bool fromJSON(const llvm::json::Value &Param,
              VariablesArguments::VariablesFilter &VA, llvm::json::Path Path);
bool fromJSON(const llvm::json::Value &, VariablesArguments &,
              llvm::json::Path);

struct VariablesResponseBody {
  std::vector<Variable> variables;
};
llvm::json::Value toJSON(const VariablesResponseBody &);

struct WriteMemoryArguments {
  addr_t memoryReference = LLDB_INVALID_ADDRESS;

  int64_t offset = 0;

  bool allowPartial = false;

  std::string data;
};
bool fromJSON(const llvm::json::Value &, WriteMemoryArguments &,
              llvm::json::Path);

struct WriteMemoryResponseBody {
  uint64_t bytesWritten = 0;
};
llvm::json::Value toJSON(const WriteMemoryResponseBody &);

struct ModuleSymbolsArguments {
  std::string moduleId;

  std::string moduleName;

  std::optional<uint32_t> startIndex;

  std::optional<uint32_t> count;
};
bool fromJSON(const llvm::json::Value &, ModuleSymbolsArguments &,
              llvm::json::Path);

struct ModuleSymbolsResponseBody {
  std::vector<Symbol> symbols;
};
llvm::json::Value toJSON(const ModuleSymbolsResponseBody &);

struct ExceptionInfoArguments {
  tid_t threadId = LLDB_INVALID_THREAD_ID;
};
bool fromJSON(const llvm::json::Value &, ExceptionInfoArguments &,
              llvm::json::Path);

struct ExceptionInfoResponseBody {
  std::string exceptionId;

  std::string description;

  ExceptionBreakMode breakMode = eExceptionBreakModeNever;

  std::optional<ExceptionDetails> details;
};
llvm::json::Value toJSON(const ExceptionInfoResponseBody &);

enum EvaluateContext : unsigned {
  eEvaluateContextUnknown = 0,
  eEvaluateContextWatch = 1,
  eEvaluateContextRepl = 2,
  eEvaluateContextHover = 3,
  eEvaluateContextClipboard = 4,
  eEvaluateContextVariables = 5,
};

struct EvaluateArguments {
  std::string expression;

  uint64_t frameId = LLDB_DAP_INVALID_FRAME_ID;

  uint32_t line = LLDB_INVALID_LINE_NUMBER;

  uint32_t column = LLDB_INVALID_COLUMN_NUMBER;

  std::optional<Source> source;

  EvaluateContext context = eEvaluateContextUnknown;

  std::optional<ValueFormat> format;
};
bool fromJSON(const llvm::json::Value &, EvaluateArguments &, llvm::json::Path);

struct EvaluateResponseBody {
  std::string result;
  std::string type;
  std::optional<VariablePresentationHint> presentationHint;
  int64_t variablesReference = 0;
  uint32_t namedVariables = 0;
  uint32_t indexedVariables = 0;
  std::string memoryReference;
  uint64_t valueLocationReference = LLDB_DAP_INVALID_VALUE_LOC;
};
llvm::json::Value toJSON(const EvaluateResponseBody &);

struct PauseArguments {
  tid_t threadId = LLDB_INVALID_THREAD_ID;
};
bool fromJSON(const llvm::json::Value &, PauseArguments &, llvm::json::Path);

using PauseResponse = VoidResponse;

struct LocationsArguments {
  uint64_t locationReference = LLDB_DAP_INVALID_VALUE_LOC;
};
bool fromJSON(const llvm::json::Value &, LocationsArguments &,
              llvm::json::Path);

struct LocationsResponseBody {
  Source source;

  uint32_t line = LLDB_INVALID_LINE_NUMBER;

  uint32_t column = LLDB_INVALID_COLUMN_NUMBER;

  uint32_t endLine = LLDB_INVALID_LINE_NUMBER;

  uint32_t endColumn = LLDB_INVALID_COLUMN_NUMBER;
};
llvm::json::Value toJSON(const LocationsResponseBody &);

struct CompileUnitsArguments {
  std::string moduleId;
};
bool fromJSON(const llvm::json::Value &, CompileUnitsArguments &,
              llvm::json::Path);

struct CompileUnitsResponseBody {
  std::vector<CompileUnit> compileUnits;
};
llvm::json::Value toJSON(const CompileUnitsResponseBody &);

using TestGetTargetBreakpointsArguments = EmptyArguments;

struct TestGetTargetBreakpointsResponseBody {
  std::vector<Breakpoint> breakpoints;
};
llvm::json::Value toJSON(const TestGetTargetBreakpointsResponseBody &);

struct RestartArguments {
  std::variant<std::monostate, LaunchRequestArguments, AttachRequestArguments>
      arguments = std::monostate{};
};
bool fromJSON(const llvm::json::Value &, RestartArguments &, llvm::json::Path);

using RestartResponse = VoidResponse;

struct StackTraceArguments {
  tid_t threadId = LLDB_INVALID_THREAD_ID;

  uint32_t startFrame = 0;

  uint32_t levels = 0;

  std::optional<StackFrameFormat> format;
};
bool fromJSON(const llvm::json::Value &, StackTraceArguments &,
              llvm::json::Path);

struct StackTraceResponseBody {
  std::vector<StackFrame> stackFrames;

  uint32_t totalFrames = 0;
};
llvm::json::Value toJSON(const StackTraceResponseBody &);

using TraceEnableArguments = EmptyArguments;
using TraceDisableArguments = EmptyArguments;
using TraceStatusArguments = EmptyArguments;

struct TraceStatusResponseBody {
    bool enabled = false;
};
llvm::json::Value toJSON(const TraceStatusResponseBody &);

using UnknownArguments = EmptyArguments;
using UnknownResponseBody = VoidResponse;
} // namespace dap::protocol

#endif  // TRAILER_DAP_PROTOCOL_PROTOCOL_REQUESTS_HPP_
