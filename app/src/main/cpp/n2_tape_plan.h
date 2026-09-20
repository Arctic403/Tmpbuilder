#pragma once

#include "n2_plan_common.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace codynex::n2 {

struct TapeInstruction {
    std::uint8_t opcode = 0U;
};

class TapePlan {
public:
    TapePlan() = default;

    static TapePlan fromRecipe(const GeneratedRecipe& recipe);

    bool empty() const;
    std::size_t bytesProxy() const;
    std::uint64_t hash() const;

    PlanExecutionResult execute(
        Mesh& mesh,
        Schedule schedule,
        std::uint32_t seed,
        std::uint32_t maxCycles,
        std::uint64_t primitiveBudget
    ) const;

private:
    std::vector<TapeInstruction> tape_;
};

}  // namespace codynex::n2
