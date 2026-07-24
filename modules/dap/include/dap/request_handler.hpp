//===-- RequestHandler.h ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_DAP_REQUEST_HANDLER_HPP_
#define TRAILER_DAP_REQUEST_HANDLER_HPP_

// The LLDB-agnostic half of the request-handling framework: the seam the
// Orchestrator dispatches through (IRequestHandler), plus the generic
// argument-parsing helper every concrete handler builds on. Services own no
// dispatch table of their own - each handler registers itself with the
// Orchestrator directly (see Orchestrator::RegisterHandler), so the
// Orchestrator never needs to know which service (if any) backs a command.
//
// The service-specific base (holding e.g. a DebugService&, adding
// service-specific helpers) lives alongside that service's own handlers -
// see debug_service's handlers/request_handler.hpp for the debug service's.

#include "dap/dap_error.hpp"
#include "dap/protocol/protocol_base.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include <optional>
#include <type_traits>

namespace dap {

template <typename T> struct is_optional : std::false_type {};

template <typename T> struct is_optional<std::optional<T>> : std::true_type {};

template <typename T> inline constexpr bool is_optional_v = is_optional<T>::value;

// Parses `request.arguments` into `Args` (an aggregate generated from the
// DAP schema, via protocol::fromJSON). `Args` is required unless it's
// std::optional<...> itself.
template <typename Args>
llvm::Expected<Args> parseArgs(const protocol::Request &request) {
    if (!is_optional_v<Args> && !request.arguments)
        return llvm::make_error<DAPError>(
            llvm::formatv("arguments required for command '{0}' but none received", request.command).str());

    Args arguments;
    llvm::json::Path::Root root("arguments");
    if (request.arguments && !fromJSON(*request.arguments, arguments, root)) {
        std::string parse_failure;
        llvm::raw_string_ostream OS(parse_failure);
        OS << "invalid arguments for request '" << request.command << "': " << llvm::toString(root.getError())
           << "\n";
        root.printErrorContext(*request.arguments, OS);
        return llvm::make_error<DAPError>(parse_failure);
    }

    return arguments;
}

template <>
inline llvm::Expected<protocol::EmptyArguments> parseArgs(const protocol::Request &request) {
    return std::nullopt;
}

// The routing seam every request handler implements. The Orchestrator holds
// these by command name (see Orchestrator::RegisterHandler) and dispatches
// directly to Run() - it never needs to know which service (if any) owns
// the handler, matching the DAP-layer principle that handlers are part of
// the communication layer, not the services they call into.
class IRequestHandler {
public:
    virtual ~IRequestHandler() = default;

    // Handles one already-routed request; implementations send their own
    // response via whatever mechanism their concrete handler holds.
    virtual void Run(const protocol::Request &request) = 0;

    // The set of DAP `Capabilities.supportedFeatures` this handler
    // contributes when present/registered - aggregated by
    // Orchestrator::AggregatedHandlerFeatures() for `initialize` responses.
    using FeatureSet = llvm::SmallDenseSet<protocol::AdapterFeature, 1>;
    virtual FeatureSet GetSupportedFeatures() const { return {}; }
};

}  // namespace dap

#endif  // TRAILER_DAP_REQUEST_HANDLER_HPP_
