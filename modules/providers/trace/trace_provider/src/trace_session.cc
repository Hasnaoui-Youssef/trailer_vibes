#include "trace_provider/trace_session.hpp"

#include <opencsd.h>

#include "trace_decoder/trace_decoder.hpp"
#include "trace_transform/function_block_transform.hpp"
#include "trace_transform/reconstructed_instruction_transform.hpp"
#include "trace_transform/trace_gap_transform.hpp"
#include "trace_transform/transform.hpp"

namespace trace_provider {

struct TraceSession::Impl {
    model::InstructionTraceDecodeConfig base_config;
    std::vector<model::LoadSegment> segments;
    ResolveInstruction resolve;
    decode::TraceDecoder decoder;
    uint64_t capture_count = 0;
    uint64_t total_executed_instructions = 0;
};

TraceSession::TraceSession(model::InstructionTraceDecodeConfig base_config, std::vector<model::LoadSegment> segments,
                            ResolveInstruction resolve)
    : impl_(std::make_unique<Impl>(Impl{
          .base_config = std::move(base_config), .segments = std::move(segments), .resolve = std::move(resolve)})) {}

TraceSession::TraceSession(TraceSession &&) noexcept = default;
TraceSession &TraceSession::operator=(TraceSession &&) noexcept = default;
TraceSession::~TraceSession() = default;

std::expected<DecodedIncrement, std::string> TraceSession::Append(std::span<const std::byte> capture) {
    std::vector<uint8_t> trace_data(reinterpret_cast<const uint8_t *>(capture.data()),
                                     reinterpret_cast<const uint8_t *>(capture.data()) + capture.size());

    model::InstructionTraceDecodeConfig::Builder builder;
    builder.SetProgramPath(impl_->base_config.program_path())
        .SetCoreName(impl_->base_config.core_name())
        .SetDeformatter(impl_->base_config.deformatter())
        .SetRegisters(impl_->base_config.registers())
        .SetTraceData(std::move(trace_data));
    model::InstructionTraceDecodeConfig config = std::move(builder).Build();

    std::expected<std::vector<trace::TraceRecord>, std::string> decode_result;
    try {
        decode_result = impl_->decoder.Decode(config, impl_->segments);
    } catch (const ocsdError &error) {
        return std::unexpected("OpenCSD decode threw: " + ocsdError::getErrorString(error));
    } catch (const std::exception &error) {
        return std::unexpected(std::string("decode threw: ") + error.what());
    } catch (...) {
        return std::unexpected("decode threw an unrecognized exception");
    }
    if (!decode_result) return std::unexpected(decode_result.error());
    const std::vector<trace::TraceRecord> &records = *decode_result;

    DecodedIncrement increment;
    uint64_t executed_count = 0;
    for (model::ReconstructedInstruction &instruction :
         xform::Transform<model::ReconstructedInstruction>(records, impl_->resolve)) {
        if (instruction.executed) ++executed_count;
        increment.instructions.push_back(std::move(instruction));
    }
    increment.function_blocks = xform::TraceTransform<model::FunctionBlock>::Apply(
        std::span<const model::ReconstructedInstruction>(increment.instructions), impl_->resolve);

    increment.gaps = xform::Transform<model::TraceGap>(records);
    for (model::TraceGap &gap : increment.gaps) gap.instruction_index += impl_->total_executed_instructions;
    if (impl_->capture_count > 0) {
        increment.gaps.insert(increment.gaps.begin(),
                               model::TraceGap{impl_->total_executed_instructions, model::GapReason::kCaptureBoundary});
    }

    impl_->capture_count += 1;
    impl_->total_executed_instructions += executed_count;

    return increment;
}

}  // namespace trace_provider
