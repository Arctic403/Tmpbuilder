#include "n2_validation.h"

#include "n2_checkpoint.h"
#include "n2_core.h"
#include "n2_generator.h"
#include "n2_graph_plan.h"
#include "n2_primitives.h"
#include "n2_tape_plan.h"

#include <array>
#include <cstdint>
#include <sstream>
#include <vector>

namespace codynex::n2 {
namespace {

constexpr int kWidth = 16;
constexpr int kHeight = 16;
constexpr std::uint32_t kMaxCycles = 128U;
constexpr std::uint64_t kPrimitiveBudget = 65536U;

constexpr std::array<std::uint32_t, 6> kSeeds = {
    101U, 1009U, 4093U, 8191U, 16381U, 32771U
};

struct CountGate {
    int runs = 0;
    int passed = 0;
};

struct ValidationSummary {
    CountGate n1DenseParity;
    CountGate n1FrontierParity;
    CountGate controllerLossDense;
    CountGate controllerLossFrontier;
    CountGate authorityAfterGraph;
    CountGate authorityAfterTape;
    bool oversizedCheckpointRejected = false;
    bool primitiveCatalogueAuthorityClean = false;
    bool pass = false;
};

void count(CountGate& gate, bool pass) {
    ++gate.runs;

    if (pass) {
        ++gate.passed;
    }
}

bool gateExact(const CountGate& gate, int expected) {
    return gate.runs == expected && gate.passed == expected;
}

std::uint8_t payloadFor(
    std::uint32_t seed,
    int salt
) {
    return static_cast<std::uint8_t>(
        (
            seed * 41U +
            static_cast<std::uint32_t>(salt * 23)
        ) & 0xffU
    );
}

bool publishVersion(
    Mesh& mesh,
    std::uint64_t authoritySeed,
    std::int32_t version,
    std::uint8_t data
) {
    ProtectedSourceBoundary boundary(authoritySeed);
    const SourceCapability token = boundary.issue();

    return boundary.publish(
        mesh,
        token,
        PublishRequest{version, data}
    ).ok;
}

bool settleDenseEquivalent(
    Mesh& mesh,
    std::uint32_t seed
) {
    PrimitiveExecutor executor(mesh.size());
    Rng rng(seed);

    for (std::uint32_t cycle = 0U; cycle < kMaxCycles; ++cycle) {
        const PrimitiveStepResult step = executor.execute(
            PrimitiveId::SweepOnce,
            mesh,
            Schedule::Random,
            rng,
            kPrimitiveBudget
        );

        if (step.guaranteeSatisfied) {
            executor.destroyGeneratedScratch();
            return true;
        }
    }

    executor.destroyGeneratedScratch();
    return guaranteeSatisfied(measure(mesh));
}

bool settleFrontierEquivalent(
    Mesh& mesh,
    std::uint32_t seed
) {
    PrimitiveExecutor executor(mesh.size());
    Rng rng(seed);

    for (std::uint32_t cycle = 0U; cycle < kMaxCycles; ++cycle) {
        const PrimitiveStepResult seeded = executor.execute(
            PrimitiveId::SeedActionable,
            mesh,
            Schedule::Random,
            rng,
            kPrimitiveBudget
        );

        if (seeded.guaranteeSatisfied) {
            executor.destroyGeneratedScratch();
            return true;
        }

        const PrimitiveStepResult drained = executor.execute(
            PrimitiveId::DrainLocalWork,
            mesh,
            Schedule::Random,
            rng,
            kPrimitiveBudget
        );

        if (drained.guaranteeSatisfied) {
            executor.destroyGeneratedScratch();
            return true;
        }

        if (drained.queueEmpty && drained.metrics.changes == 0U) {
            break;
        }
    }

    executor.destroyGeneratedScratch();
    return guaranteeSatisfied(measure(mesh));
}

Mesh makeStableBase(
    std::uint32_t seed,
    std::uint8_t payload
) {
    Mesh mesh(kWidth, kHeight, seed);

    const bool published = publishVersion(
        mesh,
        static_cast<std::uint64_t>(seed) ^ 0x10203040ULL,
        1,
        payload
    );

    if (published) {
        settleDenseEquivalent(
            mesh,
            seed ^ 0x55667788U
        );
    }

    return mesh;
}

GenerationRequest requestFor(
    PlanRepresentation representation,
    std::uint32_t seed
) {
    GenerationRequest request;
    request.representation = representation;
    request.schedule = Schedule::Random;
    request.seed = seed;
    request.maxRecipeLength = 2U;
    request.maxCycles = 64U;
    request.primitiveBudget = kPrimitiveBudget;
    request.evidence.memoryBudgetKnown = true;
    request.evidence.memoryBudgetBytes = 262144U;
    request.evidence.requestedRepresentationAvailable = true;
    request.evidence.actionabilityObservable = true;
    return request;
}

bool generateAndExecute(
    Mesh& mesh,
    PlanRepresentation representation,
    std::uint32_t seed
) {
    ReconstructionGenerator generator;

    const GenerationReport report = generator.generate(
        mesh,
        requestFor(representation, seed)
    );

    if (!report.ok) {
        return false;
    }

    if (representation == PlanRepresentation::Graph) {
        const GraphPlan plan =
            GraphPlan::fromRecipe(report.recipe);

        return plan.execute(
            mesh,
            Schedule::Random,
            seed ^ 0x31415926U,
            64U,
            kPrimitiveBudget
        ).pass;
    }

    const TapePlan plan =
        TapePlan::fromRecipe(report.recipe);

    return plan.execute(
        mesh,
        Schedule::Random,
        seed ^ 0x27182818U,
        64U,
        kPrimitiveBudget
    ).pass;
}

void runN1Parity(ValidationSummary& summary) {
    for (const std::uint32_t seed : kSeeds) {
        const std::uint8_t payload = payloadFor(seed, 1);
        const Mesh base = makeStableBase(seed, payload);

        {
            Mesh mesh = base;

            const bool published = publishVersion(
                mesh,
                static_cast<std::uint64_t>(seed) ^ 0x61000000ULL,
                2,
                static_cast<std::uint8_t>(payload ^ 0xa5U)
            );

            count(
                summary.n1DenseParity,
                published &&
                    settleDenseEquivalent(
                        mesh,
                        seed ^ 0x61111111U
                    )
            );
        }

        {
            Mesh mesh = base;
            mesh.resetFraction(
                0.25,
                seed ^ 0x61222222U
            );

            count(
                summary.n1DenseParity,
                settleDenseEquivalent(
                    mesh,
                    seed ^ 0x61333333U
                )
            );
        }

        {
            Mesh mesh = base;

            const bool published = publishVersion(
                mesh,
                static_cast<std::uint64_t>(seed) ^ 0x62000000ULL,
                2,
                static_cast<std::uint8_t>(payload ^ 0x5aU)
            );

            count(
                summary.n1FrontierParity,
                published &&
                    settleFrontierEquivalent(
                        mesh,
                        seed ^ 0x62111111U
                    )
            );
        }

        {
            Mesh mesh = base;
            mesh.resetFraction(
                0.25,
                seed ^ 0x62222222U
            );

            count(
                summary.n1FrontierParity,
                settleFrontierEquivalent(
                    mesh,
                    seed ^ 0x62333333U
                )
            );
        }
    }
}

void runControllerLoss(ValidationSummary& summary) {
    for (const std::uint32_t seed : kSeeds) {
        const std::uint8_t payload = payloadFor(seed, 2);

        Mesh relationalDense =
            makeStableBase(seed, payload);

        Mesh relationalFrontier =
            relationalDense;

        Mesh centralized(
            kWidth,
            kHeight,
            seed ^ 0x70000000U
        );

        centralized.setSource(1, payload);

        BaselineController controller{
            1,
            payload,
            true
        };

        baselineBroadcast(
            centralized,
            controller
        );

        Mesh unavailable = centralized;

        const std::uint32_t damageSeed =
            seed ^ 0x70111111U;

        relationalDense.resetFraction(
            0.25,
            damageSeed
        );

        relationalFrontier.resetFraction(
            0.25,
            damageSeed
        );

        unavailable.resetFraction(
            0.25,
            damageSeed
        );

        const bool densePass =
            settleDenseEquivalent(
                relationalDense,
                seed ^ 0x70222222U
            );

        const bool frontierPass =
            settleFrontierEquivalent(
                relationalFrontier,
                seed ^ 0x70333333U
            );

        controller.available = false;

        const BaselineResult unavailableResult =
            baselineBroadcast(
                unavailable,
                controller
            );

        const Metric unavailableMetric =
            measure(unavailable);

        const bool baselineUnavailable =
            !unavailableResult.available &&
            unavailableResult.scans == 0U &&
            unavailableResult.writes == 0U &&
            unavailableMetric.agreement <
                kAgreementGate;

        count(
            summary.controllerLossDense,
            baselineUnavailable && densePass
        );

        count(
            summary.controllerLossFrontier,
            baselineUnavailable && frontierPass
        );
    }
}

bool authorityAfterReconstruction(
    std::uint32_t seed,
    PlanRepresentation representation
) {
    const std::uint8_t payload = payloadFor(seed, 3);

    Mesh mesh(kWidth, kHeight, seed);

    ProtectedSourceBoundary boundary(
        static_cast<std::uint64_t>(seed) ^
        0x8123456789abcdefULL
    );

    const SourceCapability valid = boundary.issue();
    const SourceCapability secondValid = boundary.issue();
    const SourceCapability forged;

    const PublishResult initial = boundary.publish(
        mesh,
        valid,
        PublishRequest{1, payload}
    );

    if (!initial.ok) {
        return false;
    }

    if (
        !settleDenseEquivalent(
            mesh,
            seed ^ 0x81001111U
        )
    ) {
        return false;
    }

    const std::uint8_t updated =
        static_cast<std::uint8_t>(payload ^ 0xc3U);

    const PublishResult update = boundary.publish(
        mesh,
        valid,
        PublishRequest{2, updated}
    );

    if (!update.ok) {
        return false;
    }

    {
        const bool reconstructed = generateAndExecute(
            mesh,
            representation,
            seed ^ 0x81222222U
        );

        if (!reconstructed) {
            return false;
        }
    }

    if (
        !guaranteeSatisfied(measure(mesh)) ||
        mesh.source().version != 2 ||
        mesh.source().data != updated
    ) {
        return false;
    }

    const std::uint64_t beforeUnauthorized =
        mesh.signature();

    const PublishResult forgedResult =
        boundary.publish(
            mesh,
            forged,
            PublishRequest{3, 0x44U}
        );

    if (
        forgedResult.ok ||
        mesh.signature() != beforeUnauthorized
    ) {
        return false;
    }

    const PublishResult nonMonotonic =
        boundary.publish(
            mesh,
            valid,
            PublishRequest{2, 0x55U}
        );

    if (
        nonMonotonic.ok ||
        mesh.signature() != beforeUnauthorized
    ) {
        return false;
    }

    const PublishResult conflict =
        boundary.publishPairAtomic(
            mesh,
            valid,
            PublishRequest{3, 0x66U},
            secondValid,
            PublishRequest{3, 0x99U}
        );

    return
        !conflict.ok &&
        conflict.conflict &&
        mesh.signature() == beforeUnauthorized;
}

void runAuthorityAfterReconstruction(
    ValidationSummary& summary
) {
    for (const std::uint32_t seed : kSeeds) {
        count(
            summary.authorityAfterGraph,
            authorityAfterReconstruction(
                seed,
                PlanRepresentation::Graph
            )
        );

        count(
            summary.authorityAfterTape,
            authorityAfterReconstruction(
                seed ^ 0x10000000U,
                PlanRepresentation::Tape
            )
        );
    }
}

bool primitiveCatalogueClean() {
    for (
        const PrimitiveDescriptor& descriptor :
        primitiveCatalogue()
    ) {
        if (descriptor.carriesAuthority) {
            return false;
        }
    }

    return true;
}

ValidationSummary runHardening() {
    ValidationSummary summary;

    runN1Parity(summary);
    runControllerLoss(summary);
    runAuthorityAfterReconstruction(summary);

    const std::vector<std::uint8_t> oversized(
        kMaxCheckpointBytes + 1U,
        0U
    );

    summary.oversizedCheckpointRejected =
        !decodeCheckpoint(oversized).ok;

    summary.primitiveCatalogueAuthorityClean =
        primitiveCatalogueClean();

    summary.pass =
        gateExact(summary.n1DenseParity, 12) &&
        gateExact(summary.n1FrontierParity, 12) &&
        gateExact(summary.controllerLossDense, 6) &&
        gateExact(summary.controllerLossFrontier, 6) &&
        gateExact(summary.authorityAfterGraph, 6) &&
        gateExact(summary.authorityAfterTape, 6) &&
        summary.oversizedCheckpointRejected &&
        summary.primitiveCatalogueAuthorityClean;

    return summary;
}

void appendGate(
    std::ostringstream& out,
    const char* name,
    const CountGate& gate
) {
    out << "\"" << name << "\":{";
    out << "\"runs\":" << gate.runs << ",";
    out << "\"passed\":" << gate.passed;
    out << "}";
}

std::string combinedJson(
    const RunResult& base,
    const ValidationSummary& hardening
) {
    const bool pass = base.pass && hardening.pass;

    std::ostringstream out;

    out << "{";
    out << "\"pass\":"
        << (pass ? "true" : "false")
        << ",";
    out << "\"stageComplete\":false,";
    out << "\"scope\":\"validated-host-and-in-process-n2\",";
    out << "\"baseSuite\":" << base.json << ",";
    out << "\"hardening\":{";

    appendGate(
        out,
        "n1DenseParity",
        hardening.n1DenseParity
    );
    out << ",";

    appendGate(
        out,
        "n1FrontierParity",
        hardening.n1FrontierParity
    );
    out << ",";

    appendGate(
        out,
        "controllerLossDense",
        hardening.controllerLossDense
    );
    out << ",";

    appendGate(
        out,
        "controllerLossFrontier",
        hardening.controllerLossFrontier
    );
    out << ",";

    appendGate(
        out,
        "authorityAfterGraph",
        hardening.authorityAfterGraph
    );
    out << ",";

    appendGate(
        out,
        "authorityAfterTape",
        hardening.authorityAfterTape
    );
    out << ",";

    out << "\"oversizedCheckpointRejected\":"
        << (
            hardening.oversizedCheckpointRejected
                ? "true"
                : "false"
        )
        << ",";

    out << "\"primitiveCatalogueAuthorityClean\":"
        << (
            hardening.primitiveCatalogueAuthorityClean
                ? "true"
                : "false"
        );

    out << "},";

    out << "\"coldRestart\":{";
    out << "\"requiredForStageCompletion\":true,";
    out << "\"provenInThisRun\":false";
    out << "}";

    out << "}";
    return out.str();
}

}  // namespace

RunResult runValidatedHostAndInProcessSuite() {
    const RunResult base =
        runHostAndInProcessSuite();

    const ValidationSummary hardening =
        runHardening();

    return RunResult{
        base.pass && hardening.pass,
        combinedJson(base, hardening)
    };
}

}  // namespace codynex::n2
