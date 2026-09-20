#pragma once

#include "n2_primitives.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace codynex::n2 {

enum class PlanRepresentation : std::uint8_t {
    Graph = 1U,
    Tape = 2U
};

struct GeneratedRecipe {
    std::vector<PrimitiveId> steps;
    std::uint64_t synthesisHash = 0U;
};

struct PlanExecutionResult {
    bool pass = false;
    PrimitiveMetrics metrics;
    std::uint64_t finalSignature = 0U;
    std::uint32_t cycles = 0U;
    std::size_t planBytes = 0U;
    std::uint64_t planHash = 0U;
};

std::uint64_t hashRecipe(const std::vector<PrimitiveId>& steps);

}  // namespace codynex::n2
