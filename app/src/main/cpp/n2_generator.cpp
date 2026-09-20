#include "n2_generator.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>
#include <vector>

namespace codynex::n2 {
namespace {

struct CandidateEvaluation {
    bool pass = false;
    PrimitiveMetrics metrics;
    std::uint32_t cycles = 0U;
    std::size_t activeBytes = 0U;
    std::size_t planBytes = 0U;
    std::uint64_t score = std::numeric_limits<std::uint64_t>::max();
};

std::size_t planBytesFor(
    const GeneratedRecipe& recipe,
    PlanRepresentation representation
) {
    if (representation == PlanRepresentation::Graph) {
        return GraphPlan::fromRecipe(recipe).bytesProxy();
    }

    return TapePlan::fromRecipe(recipe).bytesProxy();
}

bool recipeNeedsActionability(
    const std::vector<PrimitiveId>& steps
) {
    return std::find(
        steps.begin(),
        steps.end(),
        PrimitiveId::SeedActionable
    ) != steps.end();
}

CandidateEvaluation evaluateRecipe(
    const Mesh& substrate,
    const GenerationRequest& request,
    const GeneratedRecipe& recipe
) {
    CandidateEvaluation evaluation;
    Mesh probe = substrate;
    PrimitiveExecutor executor(probe.size());

    evaluation.planBytes = planBytesFor(
        recipe,
        request.representation
    );

    evaluation.activeBytes =
        executor.scratchBytesProxy() +
        evaluation.planBytes;

    if (
        request.evidence.memoryBudgetKnown &&
        evaluation.activeBytes >
            request.evidence.memoryBudgetBytes
    ) {
        return evaluation;
    }

    if (
        !request.evidence.actionabilityObservable &&
        recipeNeedsActionability(recipe.steps)
    ) {
        return evaluation;
    }

    Rng rng(
        request.seed ^
        static_cast<std::uint32_t>(
            recipe.synthesisHash & 0xffffffffULL
        )
    );

    for (
        std::uint32_t cycle = 0U;
        cycle < request.maxCycles;
        ++cycle
    ) {
        for (const PrimitiveId primitive : recipe.steps) {
            const PrimitiveStepResult step = executor.execute(
                primitive,
                probe,
                request.schedule,
                rng,
                request.primitiveBudget
            );

            evaluation.metrics = combinePrimitiveMetrics(
                evaluation.metrics,
                step.metrics
            );

            if (step.guaranteeSatisfied) {
                evaluation.pass = true;
                evaluation.cycles = cycle + 1U;
                break;
            }
        }

        if (evaluation.pass) {
            break;
        }
    }

    executor.destroyGeneratedScratch();

    if (!evaluation.pass) {
        return evaluation;
    }

    const std::uint64_t queueCost =
        evaluation.metrics.queuePushes +
        evaluation.metrics.queuePops;

    evaluation.score =
        evaluation.metrics.relationEvaluations +
        (queueCost / 4U) +
        static_cast<std::uint64_t>(
            evaluation.planBytes
        );

    return evaluation;
}

}  // namespace

GenerationReport ReconstructionGenerator::generate(
    const Mesh& substrate,
    const GenerationRequest& request
) const {
    const auto started = std::chrono::steady_clock::now();

    if (!request.evidence.requestedRepresentationAvailable) {
        GenerationReport report;
        report.representation = request.representation;
        report.uncertain = true;
        report.reason = "requested-representation-unavailable";

        const auto ended = std::chrono::steady_clock::now();
        report.generationElapsedMicros =
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<
                    std::chrono::microseconds
                >(ended - started).count()
            );
        return report;
    }

    if (!request.evidence.memoryBudgetKnown) {
        return safeFallback(
            substrate,
            request,
            "memory-budget-erased"
        );
    }

    if (!request.evidence.actionabilityObservable) {
        return safeFallback(
            substrate,
            request,
            "actionability-erased"
        );
    }

    GenerationReport best;
    best.representation = request.representation;
    best.selectedScore =
        std::numeric_limits<std::uint64_t>::max();

    const auto& catalogue = primitiveCatalogue();
    std::vector<PrimitiveId> current;
    current.reserve(request.maxRecipeLength);

    std::uint64_t totalEvaluations = 0U;
    std::uint64_t totalQueueOps = 0U;
    std::uint64_t considered = 0U;
    std::uint64_t passing = 0U;
    std::size_t peakScratchBytes = 0U;

    std::function<void(std::uint32_t)> enumerate;

    enumerate = [&](std::uint32_t remaining) {
        if (!current.empty()) {
            GeneratedRecipe recipe;
            recipe.steps = current;
            recipe.synthesisHash = hashRecipe(recipe.steps);

            ++considered;

            const CandidateEvaluation candidate =
                evaluateRecipe(
                    substrate,
                    request,
                    recipe
                );

            totalEvaluations +=
                candidate.metrics.relationEvaluations;

            totalQueueOps +=
                candidate.metrics.queuePushes +
                candidate.metrics.queuePops;

            peakScratchBytes = std::max(
                peakScratchBytes,
                candidate.activeBytes
            );

            if (candidate.pass) {
                ++passing;

                const bool better =
                    !best.ok ||
                    candidate.score < best.selectedScore ||
                    (
                        candidate.score == best.selectedScore &&
                        recipe.synthesisHash <
                            best.recipe.synthesisHash
                    );

                if (better) {
                    best.ok = true;
                    best.recipe = recipe;
                    best.selectedScore = candidate.score;
                    best.selectedPlanBytes =
                        candidate.planBytes;
                }
            }
        }

        if (remaining == 0U) {
            return;
        }

        for (const PrimitiveDescriptor& descriptor : catalogue) {
            current.push_back(descriptor.id);
            enumerate(remaining - 1U);
            current.pop_back();
        }
    };

    enumerate(request.maxRecipeLength);

    best.candidatesConsidered = considered;
    best.candidatesPassing = passing;
    best.generationEvaluations = totalEvaluations;
    best.generationQueueOps = totalQueueOps;
    best.generatorScratchBytes = peakScratchBytes;

    if (!best.ok) {
        return safeFallback(
            substrate,
            request,
            "bounded-search-found-no-valid-plan"
        );
    }

    best.uncertain = false;
    best.usedFallback = false;
    best.reason = "bounded-search-selected-plan";

    const auto ended = std::chrono::steady_clock::now();
    best.generationElapsedMicros =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<
                std::chrono::microseconds
            >(ended - started).count()
        );

    return best;
}

GenerationReport ReconstructionGenerator::safeFallback(
    const Mesh& substrate,
    const GenerationRequest& request,
    const char* reason
) const {
    const auto started = std::chrono::steady_clock::now();

    GenerationReport report;
    report.representation = request.representation;
    report.uncertain = true;
    report.usedFallback = true;
    report.reason = reason == nullptr
        ? "uncertain-fallback"
        : reason;

    if (!request.evidence.requestedRepresentationAvailable) {
        return report;
    }

    GeneratedRecipe recipe;
    recipe.steps.push_back(PrimitiveId::SweepOnce);
    recipe.synthesisHash = hashRecipe(recipe.steps);

    GenerationRequest fallbackRequest = request;
    fallbackRequest.evidence.actionabilityObservable = false;

    const CandidateEvaluation candidate =
        evaluateRecipe(
            substrate,
            fallbackRequest,
            recipe
        );

    report.candidatesConsidered = 1U;
    report.candidatesPassing = candidate.pass ? 1U : 0U;
    report.generationEvaluations =
        candidate.metrics.relationEvaluations;
    report.generationQueueOps =
        candidate.metrics.queuePushes +
        candidate.metrics.queuePops;
    report.generatorScratchBytes =
        candidate.activeBytes;
    report.selectedPlanBytes =
        candidate.planBytes;
    report.selectedScore =
        candidate.score;

    if (candidate.pass) {
        report.ok = true;
        report.recipe = recipe;
    }

    const auto ended = std::chrono::steady_clock::now();
    report.generationElapsedMicros =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<
                std::chrono::microseconds
            >(ended - started).count()
        );

    return report;
}

}  // namespace codynex::n2
