#ifndef TRAILER_TRACE_MODEL_LOAD_SEGMENT_HPP_
#define TRAILER_TRACE_MODEL_LOAD_SEGMENT_HPP_

#include <cstdint>

namespace model {

// One loadable (PT_LOAD) segment of a firmware image: where its bytes live
// in the image file, and the virtual address it occupies at runtime.
// Produced by the disassembler module from ELF program headers; consumed by
// trace_decoder to register the image's opcode bytes with the OpenCSD
// decode tree's memory accessor. Deliberately independent of both LLVM's
// object-file types and OpenCSD's memory-region types.
struct LoadSegment {
    uint64_t vaddr;        // Runtime (virtual) load address.
    uint64_t file_offset;  // Byte offset of the segment's contents in the image file.
    uint64_t size;         // Segment size in bytes, in both the file and memory.
};

}  // namespace model

#endif  // TRAILER_TRACE_MODEL_LOAD_SEGMENT_HPP_
