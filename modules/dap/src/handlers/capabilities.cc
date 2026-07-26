#include "handlers/capabilities.hpp"

#include "llvm/ADT/DenseSet.h"

namespace dap {

protocol::Capabilities AssembleCapabilities(Orchestrator &orchestrator, core::DebugContext &context) {
    protocol::Capabilities capabilities;

    capabilities.supportedFeatures = {
        protocol::eAdapterFeatureLogPoints,
        protocol::eAdapterFeatureSteppingGranularity,
        protocol::eAdapterFeatureValueFormattingOptions,
    };

    IRequestHandler::FeatureSet handler_features = orchestrator.AggregatedHandlerFeatures();
    capabilities.supportedFeatures.insert(handler_features.begin(), handler_features.end());

    capabilities.exceptionBreakpointFilters = context.ExceptionBreakpointFilters();
    capabilities.completionTriggerCharacters = {".", " ", "\t"};
    capabilities.lldbExtVersion = context.LldbVersionString();

    return capabilities;
}

protocol::Capabilities AssembleCustomCapabilities(Orchestrator &orchestrator) {
    protocol::Capabilities capabilities;
    const llvm::DenseSet<protocol::AdapterFeature> all_custom_features = {
        protocol::eAdapterFeatureSupportsModuleSymbolsRequest,
    };
    for (const protocol::AdapterFeature &feature : orchestrator.AggregatedHandlerFeatures()) {
        if (all_custom_features.contains(feature)) {
            capabilities.supportedFeatures.insert(feature);
        }
    }
    return capabilities;
}

}  // namespace dap
