#include "n2_tape_plan.h"

namespace codynex::n2 {
namespace {

std::uint8_t encodeOpcode(PrimitiveId id) {
    return static_cast<std::uint8_t>(id);
}

PrimitiveId decodeOpcode(std::uint8_t opcode) {
    switch (opcode) {
        case static_cast<std::uint8_t>(PrimitiveId::SweepOnce):
            return PrimitiveId::SweepOnce;
        case static_cast<std::uint8_t>(PrimitiveId::SeedActionable):
            return PrimitiveId::SeedActionable;
        case static_cast<std::uint8_t>(PrimitiveId::DrainLocalWork):
            return PrimitiveId::DrainLocalWork;
        default:
            return PrimitiveId::SweepOnce;
    }
}

}  // namespace

TapePlan TapePlan::fromRecipe(const GeneratedRecipe& recipe) {
    TapePlan plan;
    plan.tape_.reserve(recipe.steps.size());

    for (const PrimitiveId id : recipe.steps) {
        TapeInstruction instruction;
        instruction.opcode = encodeOpcode(id);
        plan.tape_.push_back(instruction);
    }

    return plan;
}

bool TapePlan::empty() const {
    return tape_.empty();
}

std::size_t TapePlan::bytesProxy() const {
    return tape_.capacity() * sizeof(TapeInstruction);
}

std::uint64_t TapePlan::hash() const {
    std::uint64_t value = 1469598103934665603ULL;

    for (const TapeInstruction& instruction : tape_) {
        value ^= static_cast<std::uint64_t>(instruction.opcode);
        value *= 1099511628211ULL;
    }

    value ^= static_cast<std::uint64_t>(tape_.size());
    value *= 1099511628211ULL;
    return value;
}

PlanExecutionResult TapePlan::execute(
    Mesh& mesh,
    Schedule schedule,
    std::uint32_t seed,
    std::uint32_t maxCycles,
    std::uint64_t primitiveBudget
) const {
    PlanExecutionResult result;
    result.planBytes = bytesProxy();
    result.planHash = hash();

    if (tape_.empty()) {
        result.finalSignature = mesh.signature();
        return result;
    }

    PrimitiveExecutor executor(mesh.size());
    Rng rng(seed);

    std::size_t pc = 0U;
    std::uint32_t completedCycles = 0U;

    while (completedCycles < maxCycles) {
        if (pc >= tape_.size()) {
            pc = 0U;
            ++completedCycles;
            continue;
        }

        const PrimitiveId primitive =
            decodeOpcode(tape_[pc].opcode);

        const PrimitiveStepResult step = executor.execute(
            primitive,
            mesh,
            schedule,
            rng,
            primitiveBudget
        );

        result.metrics = combinePrimitiveMetrics(
            result.metrics,
            step.metrics
        );

        if (step.guaranteeSatisfied) {
            result.pass = true;
            break;
        }

        ++pc;

        if (pc >= tape_.size()) {
            pc = 0U;
            ++completedCycles;
        }
    }

    result.cycles = completedCycles;
    result.finalSignature = mesh.signature();

    if (!result.pass) {
        result.pass = guaranteeSatisfied(measure(mesh));
    }

    executor.destroyGeneratedScratch();
    return result;
}

}  // namespace codynex::n2
