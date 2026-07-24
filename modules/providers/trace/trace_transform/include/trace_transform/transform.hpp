#ifndef TRAILER_TRACE_TRANSFORM_TRANSFORM_HPP_
#define TRAILER_TRACE_TRANSFORM_TRANSFORM_HPP_

#include <concepts>
#include <span>
#include <utility>
#include <vector>

#include "trace_sink/trace_record.hpp"

namespace xform {

// The engine's one foundational artifact is std::vector<trace::TraceRecord>
// (see trace_sink) - everything else (a flat executed-instruction view,
// source/line/function grouping, and future end points such as replay
// state or MMIO-annotated traces) is a derived projection that some end
// point wants to consume in its own shape. Rather than one bespoke
// free-function API per projection, every such projection is expressed as
// a specialization of this one customization point, parameterized on the
// desired output type T:
//
//   template <>
//   struct TraceTransform<model::SomeOutputType> {
//       static std::vector<model::SomeOutputType> Apply(
//           std::span<const trace::TraceRecord> records, /* extra args as needed */);
//   };
//
// The mapping from `records` to `vector<T>` need not be one-to-one - one
// TraceRecord instruction range typically expands into many T's, and a
// grouping transform typically collapses many T's into fewer.
//
// Primary template intentionally left undefined: any T without a
// specialization is a compile error (an incomplete-type use of
// TraceTransform<T>), not a silently-wrong default. Concrete
// specializations live in their own headers, one per T, so a caller only
// pays for (includes) the ones it actually uses.
template <typename T>
struct TraceTransform;

// Constrains Transform<T, Args...> below so that calling it for a T with
// no matching TraceTransform<T>::Apply(records, args...) fails immediately
// and readably at the call site, instead of deep inside template
// instantiation with an "incomplete type" error.
template <typename T, typename... Args>
concept TransformableFrom = requires(std::span<const trace::TraceRecord> records, Args &&...args) {
    { TraceTransform<T>::Apply(records, std::forward<Args>(args)...) } -> std::same_as<std::vector<T>>;
};

// The one uniform entry point every end-point stage calls: "give me a
// vector<T> built from these TraceRecords." How that happens - one-to-one,
// one-to-many, or requiring extra context (e.g. a Resolve callable wrapping
// disasm::ProgramDisassembler::InstructionInfoAt) - is entirely up to
// TraceTransform<T>::Apply; this function only forwards. Args are
// forwarded so different T's can require different extra parameters
// without changing this signature.
template <typename T, typename... Args>
    requires TransformableFrom<T, Args...>
std::vector<T> Transform(std::span<const trace::TraceRecord> records, Args &&...args) {
    return TraceTransform<T>::Apply(records, std::forward<Args>(args)...);
}

}  // namespace xform

#endif  // TRAILER_TRACE_TRANSFORM_TRANSFORM_HPP_
