#include "n2_graph_plan.h"

namespace codynex::n2 {

GraphPlan GraphPlan::fromRecipe(const GeneratedRecipe& recipe) {
    GraphPlan plan;

    if (recipe.steps.empty()) {
        return plan;
    }

    plan.nodes_.reserve(recipe.steps.size());

    for (std::size_t i = 0U; i < recipe.steps.size(); ++i) {
        GraphPlanNode node;
        node.primitive = recipe.steps[i];
        node.next = static_cast<std::uint32_t>(
            (i + 1U) % recipe.steps.size()
        );
        plan.nodes_.push_back(node);
    }

    return plan;
}

bool GraphPlan::empty() const {
    return nodes_.empty();
}

std::size_t GraphPlan::bytesProxy() const {
    return nodes_.capacity() * sizeof(GraphPlanNode);
}

std::uint64_t GraphPlan::hash() const {
    std::uint64_t value = 1469598103934665603ULL;

    for (const GraphPlanNode& node : nodes_) {
        value ^= static_cast<std::uint64_t>(node.primitive);
        value *= 1099511628211ULL;
        value ^= static_cast<std::uint64_t>(node.next);
        value *= 1099511628211ULL;
    }

    value ^= static_cast<std::uint64_t>(nodes_.size());
    value *= 1099511628211ULL;
    return value;
}

PlanExecutionResult GraphPlan::execute(
    Mesh& mesh,
    Schedule schedule,
    std::uint32_t seed,
    std::uint32_t maxCycles,
    std::uint64_t primitiveBudget
) const {
    PlanExecutionResult result;
    result.planBytes = bytesProxy();
    result.planHash = hash();

    if (nodes_.empty()) {
        result.finalSignature = mesh.signature();
        return result;
    }

    PrimitiveExecutor executor(mesh.size());
    Rng rng(seed);

    std::size_t current = 0U;
    std::uint32_t completedCycles = 0U;

    while (completedCycles < maxCycles) {
        if (current >= nodes_.size()) {
            break;
        }

        const GraphPlanNode& node = nodes_[current];

        const PrimitiveStepResult step = executor.execute(
            node.primitive,
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

        const std::size_t next =
            static_cast<std::size_t>(node.next);

        if (next >= nodes_.size()) {
            break;
        }

        current = next;

        if (current == 0U) {
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
