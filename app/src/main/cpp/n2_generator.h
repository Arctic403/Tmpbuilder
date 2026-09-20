#pragma once

#include "n2_graph_plan.h"
#include "n2_tape_plan.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace codynex::n2 {

struct GenerationEvidence {
    bool memoryBudgetKnown = true;
    std::size_t memoryBudgetBytes = 262144U;
    bool requestedRepresentationAvailable = true;
    bool actionabilityObservable = true;
};

struct GenerationRequest {
    PlanRepresentation representation = PlanRepresentation::Graph;
    Schedule schedule = Schedule::Random;
    std::uint32_t seed = 1U;
    std::uint32_t maxRecipeLength = 3U;
    std::uint32_t maxCycles = 128U;
    std::uint64_t primitiveBudget = 65536U;
    GenerationEvidence evidence;
};

struct GenerationReport {
    bool ok = false;
    bool uncertain = false;
    bool usedFallback = false;
    PlanRepresentation representation = PlanRepresentation::Graph;
    GeneratedRecipe recipe;
    std::uint64_t candidatesConsidered = 0U;
    std::uint64_t candidatesPassing = 0U;
    std::uint64_t generationEvaluations = 0U;
    std::uint64_t generationQueueOps = 0U;
    std::uint64_t generationElapsedMicros = 0U;
    std::uint64_t selectedScore = 0U;
    std::size_t selectedPlanBytes = 0U;
    std::size_t generatorScratchBytes = 0U;
    std::string reason;
};

class ReconstructionGenerator {
public:
    GenerationReport generate(
        const Mesh& substrate,
        const GenerationRequest& request
    ) const;

private:
    GenerationReport safeFallback(
        const Mesh& substrate,
        const GenerationRequest& request,
        const char* reason
    ) const;
};

}  // namespace codynex::n2
