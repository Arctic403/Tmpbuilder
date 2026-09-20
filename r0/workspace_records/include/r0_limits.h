#pragma once

#include <cstddef>
#include <cstdint>

namespace codynex::r0::workspace_records {

// R0.1 laboratory resource gates.
//
// These mirror the measured production operating envelope where the R0.1
// specification explicitly requires parity of bounds. They are experiment
// limits, not permanent Codynex architecture.
inline constexpr std::size_t kMaxSimilarityCandidatesPerSide = 64U;
inline constexpr std::uint64_t kMaxSimilarityComparisons = 1024U;
inline constexpr std::uint64_t kMaxExactDiffMatrixCells = 250000U;
inline constexpr std::uint32_t kMaxDiffRecursionDepth = 64U;
inline constexpr std::size_t kMaxDiffChars = 64000U;
inline constexpr std::size_t kMaxChangedLines = 420U;
inline constexpr std::size_t kMaxCoreDataBytes = 1024U * 1024U;

}  // namespace codynex::r0::workspace_records
