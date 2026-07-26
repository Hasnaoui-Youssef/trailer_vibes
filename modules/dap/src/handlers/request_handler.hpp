//===-- Request.h ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_DAP_HANDLER_HANDLER_H
#define LLDB_TOOLS_LLDB_DAP_HANDLER_HANDLER_H

#include "core/debug_context.hpp"
#include "dap/dap_error.hpp"
#include "dap/orchestrator.hpp"
#include "dap/protocol/protocol_base.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "dap/request_handler.hpp"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

namespace dap {

/// Base class for every concrete DAP request handler. Do not extend this
/// directly: extend the RequestHandler template subclass instead.
///
/// Implements dap::IRequestHandler (the seam the Orchestrator dispatches
/// through). Holds an Orchestrator& (communication: sending
/// responses/events, cancellation, client feature negotiation) and a
/// core::DebugContext& (domain: the debug session and its components) -
/// see CLAUDE.md's layering rule that no layer above core/providers may
/// know about the LLDB SB API. No lock of any kind is held here: every
/// core method a handler calls locks internally, for its own duration
/// (see core/debug_context.hpp).
class BaseRequestHandler : public dap::IRequestHandler {
public:
  BaseRequestHandler(Orchestrator &orchestrator, core::DebugContext &context)
      : orchestrator_(orchestrator), context_(context) {}

  /// BaseRequestHandler are not copyable.
  /// @{
  BaseRequestHandler(const BaseRequestHandler &) = delete;
  BaseRequestHandler &operator=(const BaseRequestHandler &) = delete;
  /// @}

  ~BaseRequestHandler() override = default;

  void Run(const protocol::Request &) override;

  virtual void operator()(const protocol::Request &request) const = 0;

protected:
  /// Helpers used by multiple request handlers.
  /// @{

  /// Prints a welcome message on the editor if the preprocessor variable
  /// LLDB_DAP_WELCOME_MESSAGE is defined.
  void PrintWelcomeMessage() const;

  /// Prints an introduction to the debug console and information about the
  /// debug session.
  void PrintIntroductionMessage() const;

  // Takes a LaunchRequest object and launches the process, also handling
  // runInTerminal if applicable. It doesn't do any of the additional
  // initialization and bookkeeping stuff that is needed for `request_launch`.
  // This way we can reuse the process launching logic for RestartRequest too.
  llvm::Error
  LaunchProcess(const protocol::LaunchRequestArguments &request) const;

  // Check if the step-granularity is `instruction`.
  bool HasInstructionGranularity(const llvm::json::Object &request) const;

  /// @}

  /// Builds an error response from the given error.
  void BuildErrorResponse(llvm::Error, protocol::Response &) const;

  /// Sends an error response from the current handler.
  void SendError(llvm::Error, protocol::Response &) const;

  /// Sends a successful response, with an optional body from the current
  /// handler.
  void SendSuccess(protocol::Response &,
                   std::optional<llvm::json::Value> = std::nullopt) const;

  /// Send a response to the client.
  void Send(protocol::Response &response) const;

  Orchestrator &orchestrator_;
  core::DebugContext &context_;
};

/// Base class for handling DebugService requests. Handlers should declare their
/// arguments and response body types like:
///
/// class MyRequestHandler : public RequestHandler<Arguments, Response> {
///   ....
/// };
template <typename Args, typename Resp>
class RequestHandler : public BaseRequestHandler {
  using BaseRequestHandler::BaseRequestHandler;

  void operator()(const protocol::Request &request) const override {
    protocol::Response response;
    response.request_seq = request.seq;
    response.command = request.command;

    llvm::Expected<Args> arguments = dap::parseArgs<Args>(request);
    if (llvm::Error err = arguments.takeError())
      return SendError(std::move(err), response);

    if constexpr (std::is_same_v<Resp, llvm::Error>) {
      if (llvm::Error err = Run(*arguments))
        SendError(std::move(err), response);
      else
        SendSuccess(response);
    } else {
      Resp body = Run(*arguments);
      if (llvm::Error err = body.takeError())
        SendError(std::move(err), response);
      else
        SendSuccess(response, std::move(*body));
    }

    PostRun();
  };

protected:
  /// Run the request handler.
  virtual Resp Run(const Args &) const = 0;

  /// A hook for a request handler to run additional operations after the
  /// request response is sent but before the next request handler.
  ///
  /// *NOTE*: PostRun will be invoked even if the `Run` operation returned an
  /// error.
  virtual void PostRun() const {};
};

/// A specialized base class for attach and launch requests that delays sending
/// the response until 'configurationDone' is received.
template <typename Args, typename Resp>
class DelayedResponseRequestHandler : public BaseRequestHandler {
  using BaseRequestHandler::BaseRequestHandler;

  void operator()(const protocol::Request &request) const override {
    // Only support void responses for now.
    static_assert(std::is_same_v<Resp, llvm::Error>);

    protocol::Response response;
    response.request_seq = request.seq;
    response.command = request.command;

    llvm::Expected<Args> arguments = dap::parseArgs<Args>(request);
    if (llvm::Error err = arguments.takeError())
      return SendError(std::move(err), response);

    BuildErrorResponse(Run(*arguments), response);

    orchestrator_.SetDeferredConfigurationResponse([this, response]() mutable { Send(response); });

    // The 'configurationDone' request is not sent until after 'initialized'
    // triggers the breakpoints being sent and 'configurationDone' is the last
    // message in the chain.
    protocol::Event initialized{"initialized"};
    orchestrator_.Send(initialized);
  };

protected:
  /// Run the request handler.
  virtual Resp Run(const Args &) const = 0;
};

class AttachRequestHandler
    : public DelayedResponseRequestHandler<protocol::AttachRequestArguments,
                                           protocol::AttachResponse> {
public:
  using DelayedResponseRequestHandler::DelayedResponseRequestHandler;
  static llvm::StringLiteral GetCommand() { return "attach"; }
  protocol::AttachResponse
  Run(const protocol::AttachRequestArguments &args) const override;
};

class BreakpointLocationsRequestHandler
    : public RequestHandler<
          protocol::BreakpointLocationsArguments,
          llvm::Expected<protocol::BreakpointLocationsResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "breakpointLocations"; }
  FeatureSet GetSupportedFeatures() const override {
    return {protocol::eAdapterFeatureBreakpointLocationsRequest};
  }
  llvm::Expected<protocol::BreakpointLocationsResponseBody>
  Run(const protocol::BreakpointLocationsArguments &args) const override;
};

class CompletionsRequestHandler
    : public RequestHandler<protocol::CompletionsArguments,
                            llvm::Expected<protocol::CompletionsResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "completions"; }
  FeatureSet GetSupportedFeatures() const override {
    return {protocol::eAdapterFeatureCompletionsRequest};
  }
  llvm::Expected<protocol::CompletionsResponseBody>
  Run(const protocol::CompletionsArguments &args) const override;
};

class ContinueRequestHandler
    : public RequestHandler<protocol::ContinueArguments,
                            llvm::Expected<protocol::ContinueResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "continue"; }
  llvm::Expected<protocol::ContinueResponseBody>
  Run(const protocol::ContinueArguments &args) const override;
};

class ConfigurationDoneRequestHandler
    : public RequestHandler<protocol::ConfigurationDoneArguments,
                            protocol::ConfigurationDoneResponse> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "configurationDone"; }
  FeatureSet GetSupportedFeatures() const override {
    return {protocol::eAdapterFeatureConfigurationDoneRequest};
  }
  protocol::ConfigurationDoneResponse
  Run(const protocol::ConfigurationDoneArguments &) const override;
  void PostRun() const override;
};

class DisconnectRequestHandler
    : public RequestHandler<std::optional<protocol::DisconnectArguments>,
                            protocol::DisconnectResponse> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "disconnect"; }
  FeatureSet GetSupportedFeatures() const override {
    return {protocol::eAdapterFeatureTerminateDebuggee};
  }
  llvm::Error
  Run(const std::optional<protocol::DisconnectArguments> &args) const override;
};

class EvaluateRequestHandler
    : public RequestHandler<protocol::EvaluateArguments,
                            llvm::Expected<protocol::EvaluateResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "evaluate"; }
  llvm::Expected<protocol::EvaluateResponseBody>
  Run(const protocol::EvaluateArguments &) const override;
  FeatureSet GetSupportedFeatures() const override {
    return {protocol::eAdapterFeatureEvaluateForHovers,
            protocol::eAdapterFeatureClipboardContext};
  }
};

class ExceptionInfoRequestHandler final
    : public RequestHandler<
          protocol::ExceptionInfoArguments,
          llvm::Expected<protocol::ExceptionInfoResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "exceptionInfo"; }
  FeatureSet GetSupportedFeatures() const override {
    return {protocol::eAdapterFeatureExceptionInfoRequest};
  }
  llvm::Expected<protocol::ExceptionInfoResponseBody>
  Run(const protocol::ExceptionInfoArguments &args) const override;
};

class InitializeRequestHandler
    : public RequestHandler<protocol::InitializeRequestArguments,
                            llvm::Expected<protocol::InitializeResponse>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "initialize"; }
  llvm::Expected<protocol::InitializeResponse>
  Run(const protocol::InitializeRequestArguments &args) const override;
};

class LaunchRequestHandler
    : public DelayedResponseRequestHandler<protocol::LaunchRequestArguments,
                                           protocol::LaunchResponse> {
public:
  using DelayedResponseRequestHandler::DelayedResponseRequestHandler;
  static llvm::StringLiteral GetCommand() { return "launch"; }
  protocol::LaunchResponse
  Run(const protocol::LaunchRequestArguments &arguments) const override;
};

class RestartRequestHandler
    : public RequestHandler<std::optional<protocol::RestartArguments>,
                            protocol::RestartResponse> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "restart"; }
  llvm::Error
  Run(const std::optional<protocol::RestartArguments> &args) const override;
};

class NextRequestHandler
    : public RequestHandler<protocol::NextArguments, protocol::NextResponse> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "next"; }
  llvm::Error Run(const protocol::NextArguments &args) const override;
};

class StepInRequestHandler : public RequestHandler<protocol::StepInArguments,
                                                   protocol::StepInResponse> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "stepIn"; }
  llvm::Error Run(const protocol::StepInArguments &args) const override;
};

class StepInTargetsRequestHandler
    : public RequestHandler<
          protocol::StepInTargetsArguments,
          llvm::Expected<protocol::StepInTargetsResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "stepInTargets"; }
  llvm::Expected<protocol::StepInTargetsResponseBody>
  Run(const protocol::StepInTargetsArguments &args) const override;
};

class StepOutRequestHandler : public RequestHandler<protocol::StepOutArguments,
                                                    protocol::StepOutResponse> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "stepOut"; }
  llvm::Error Run(const protocol::StepOutArguments &args) const override;
};

class SetBreakpointsRequestHandler
    : public RequestHandler<
          protocol::SetBreakpointsArguments,
          llvm::Expected<protocol::SetBreakpointsResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "setBreakpoints"; }
  FeatureSet GetSupportedFeatures() const override {
    return {protocol::eAdapterFeatureConditionalBreakpoints,
            protocol::eAdapterFeatureHitConditionalBreakpoints};
  }
  llvm::Expected<protocol::SetBreakpointsResponseBody>
  Run(const protocol::SetBreakpointsArguments &args) const override;
};

class SetExceptionBreakpointsRequestHandler
    : public RequestHandler<
          protocol::SetExceptionBreakpointsArguments,
          llvm::Expected<protocol::SetExceptionBreakpointsResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "setExceptionBreakpoints"; }
  FeatureSet GetSupportedFeatures() const override {
    /// Prefer the `filterOptions` feature over the `exceptionOptions`.
    /// exceptionOptions is not supported in VSCode, while `filterOptions` is
    /// supported.
    return {protocol::eAdapterFeatureExceptionFilterOptions};
  }
  llvm::Expected<protocol::SetExceptionBreakpointsResponseBody>
  Run(const protocol::SetExceptionBreakpointsArguments &args) const override;
};

class SetFunctionBreakpointsRequestHandler
    : public RequestHandler<
          protocol::SetFunctionBreakpointsArguments,
          llvm::Expected<protocol::SetFunctionBreakpointsResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "setFunctionBreakpoints"; }
  FeatureSet GetSupportedFeatures() const override {
    return {protocol::eAdapterFeatureFunctionBreakpoints};
  }
  llvm::Expected<protocol::SetFunctionBreakpointsResponseBody>
  Run(const protocol::SetFunctionBreakpointsArguments &args) const override;
};

class DataBreakpointInfoRequestHandler
    : public RequestHandler<
          protocol::DataBreakpointInfoArguments,
          llvm::Expected<protocol::DataBreakpointInfoResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "dataBreakpointInfo"; }
  FeatureSet GetSupportedFeatures() const override {
    return {protocol::eAdapterFeatureDataBreakpointBytes};
  }
  llvm::Expected<protocol::DataBreakpointInfoResponseBody>
  Run(const protocol::DataBreakpointInfoArguments &args) const override;
};

class SetDataBreakpointsRequestHandler
    : public RequestHandler<
          protocol::SetDataBreakpointsArguments,
          llvm::Expected<protocol::SetDataBreakpointsResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "setDataBreakpoints"; }
  FeatureSet GetSupportedFeatures() const override {
    return {protocol::eAdapterFeatureDataBreakpoints};
  }
  llvm::Expected<protocol::SetDataBreakpointsResponseBody>
  Run(const protocol::SetDataBreakpointsArguments &args) const override;
};

class SetInstructionBreakpointsRequestHandler
    : public RequestHandler<
          protocol::SetInstructionBreakpointsArguments,
          llvm::Expected<protocol::SetInstructionBreakpointsResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() {
    return "setInstructionBreakpoints";
  }
  FeatureSet GetSupportedFeatures() const override {
    return {protocol::eAdapterFeatureInstructionBreakpoints};
  }
  llvm::Expected<protocol::SetInstructionBreakpointsResponseBody>
  Run(const protocol::SetInstructionBreakpointsArguments &args) const override;
};

class CompileUnitsRequestHandler
    : public RequestHandler<
          std::optional<protocol::CompileUnitsArguments>,
          llvm::Expected<protocol::CompileUnitsResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "compileUnits"; }
  llvm::Expected<protocol::CompileUnitsResponseBody>
  Run(const std::optional<protocol::CompileUnitsArguments> &args)
      const override;
};

class ModulesRequestHandler final
    : public RequestHandler<std::optional<protocol::ModulesArguments>,
                            llvm::Expected<protocol::ModulesResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "modules"; }
  FeatureSet GetSupportedFeatures() const override {
    return {protocol::eAdapterFeatureModulesRequest};
  }
  llvm::Expected<protocol::ModulesResponseBody>
  Run(const std::optional<protocol::ModulesArguments> &args) const override;
};

class PauseRequestHandler
    : public RequestHandler<protocol::PauseArguments, protocol::PauseResponse> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "pause"; }
  llvm::Error Run(const protocol::PauseArguments &args) const override;
};

class ScopesRequestHandler final
    : public RequestHandler<protocol::ScopesArguments,
                            llvm::Expected<protocol::ScopesResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "scopes"; }

  llvm::Expected<protocol::ScopesResponseBody>
  Run(const protocol::ScopesArguments &args) const override;
};

class SetVariableRequestHandler final
    : public RequestHandler<protocol::SetVariableArguments,
                            llvm::Expected<protocol::SetVariableResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "setVariable"; }
  FeatureSet GetSupportedFeatures() const override {
    return {protocol::eAdapterFeatureSetVariable};
  }
  llvm::Expected<protocol::SetVariableResponseBody>
  Run(const protocol::SetVariableArguments &args) const override;
};

class SourceRequestHandler final
    : public RequestHandler<protocol::SourceArguments,
                            llvm::Expected<protocol::SourceResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "source"; }
  llvm::Expected<protocol::SourceResponseBody>
  Run(const protocol::SourceArguments &args) const override;
};

class StackTraceRequestHandler
    : public RequestHandler<protocol::StackTraceArguments,
                            llvm::Expected<protocol::StackTraceResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "stackTrace"; }
  llvm::Expected<protocol::StackTraceResponseBody>
  Run(const protocol::StackTraceArguments &args) const override;
  FeatureSet GetSupportedFeatures() const override {
    return {protocol::eAdapterFeatureDelayedStackTraceLoading};
  }
};

class ThreadsRequestHandler
    : public RequestHandler<protocol::ThreadsArguments,
                            llvm::Expected<protocol::ThreadsResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "threads"; }
  llvm::Expected<protocol::ThreadsResponseBody>
  Run(const protocol::ThreadsArguments &) const override;
};

class VariablesRequestHandler
    : public RequestHandler<protocol::VariablesArguments,
                            llvm::Expected<protocol::VariablesResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "variables"; }
  llvm::Expected<protocol::VariablesResponseBody>
  Run(const protocol::VariablesArguments &) const override;
};

class LocationsRequestHandler
    : public RequestHandler<protocol::LocationsArguments,
                            llvm::Expected<protocol::LocationsResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "locations"; }
  llvm::Expected<protocol::LocationsResponseBody>
  Run(const protocol::LocationsArguments &) const override;
};

class DisassembleRequestHandler final
    : public RequestHandler<protocol::DisassembleArguments,
                            llvm::Expected<protocol::DisassembleResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "disassemble"; }
  FeatureSet GetSupportedFeatures() const override {
    return {protocol::eAdapterFeatureDisassembleRequest};
  }
  llvm::Expected<protocol::DisassembleResponseBody>
  Run(const protocol::DisassembleArguments &args) const override;
};

class ReadMemoryRequestHandler final
    : public RequestHandler<protocol::ReadMemoryArguments,
                            llvm::Expected<protocol::ReadMemoryResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "readMemory"; }
  FeatureSet GetSupportedFeatures() const override {
    return {protocol::eAdapterFeatureReadMemoryRequest};
  }
  llvm::Expected<protocol::ReadMemoryResponseBody>
  Run(const protocol::ReadMemoryArguments &args) const override;
};

class CancelRequestHandler : public RequestHandler<protocol::CancelArguments,
                                                   protocol::CancelResponse> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "cancel"; }
  FeatureSet GetSupportedFeatures() const override {
    return {protocol::eAdapterFeatureCancelRequest};
  }
  llvm::Error Run(const protocol::CancelArguments &args) const override;
};

class ModuleSymbolsRequestHandler
    : public RequestHandler<
          protocol::ModuleSymbolsArguments,
          llvm::Expected<protocol::ModuleSymbolsResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "__lldb_moduleSymbols"; }
  FeatureSet GetSupportedFeatures() const override {
    return {protocol::eAdapterFeatureSupportsModuleSymbolsRequest};
  }
  llvm::Expected<protocol::ModuleSymbolsResponseBody>
  Run(const protocol::ModuleSymbolsArguments &args) const override;
};

class TestGetTargetBreakpointsRequestHandler
    : public RequestHandler<
          protocol::TestGetTargetBreakpointsArguments,
          llvm::Expected<protocol::TestGetTargetBreakpointsResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() {
    return "_testGetTargetBreakpoints";
  }
  llvm::Expected<protocol::TestGetTargetBreakpointsResponseBody>
  Run(const protocol::TestGetTargetBreakpointsArguments &args) const override;
};

class WriteMemoryRequestHandler final
    : public RequestHandler<protocol::WriteMemoryArguments,
                            llvm::Expected<protocol::WriteMemoryResponseBody>> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "writeMemory"; }
  FeatureSet GetSupportedFeatures() const override {
    return {protocol::eAdapterFeatureWriteMemoryRequest};
  }
  llvm::Expected<protocol::WriteMemoryResponseBody>
  Run(const protocol::WriteMemoryArguments &args) const override;
};

class UnknownRequestHandler final
    : public RequestHandler<protocol::UnknownArguments,
                            protocol::UnknownResponseBody> {
public:
  using RequestHandler::RequestHandler;
  static llvm::StringLiteral GetCommand() { return "unknown"; }
  llvm::Error Run(const protocol::UnknownArguments &args) const override;
};

} // namespace dap

#endif
