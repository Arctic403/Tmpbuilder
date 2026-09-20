#include "n2_suite.h"

#include "n2_checkpoint.h"
#include "n2_core.h"
#include "n2_generator.h"
#include "n2_graph_plan.h"
#include "n2_primitives.h"
#include "n2_tape_plan.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace codynex::n2 {
namespace {

constexpr int kWidth = 16;
constexpr int kHeight = 16;
constexpr std::uint32_t kMaxCycles = 64U;
constexpr std::uint64_t kPrimitiveBudget = 65536U;
constexpr std::size_t kCoreDataGate = 262144U;
constexpr std::size_t kCheckpointGate = 8192U;

constexpr std::array<std::uint32_t, 6> kSeeds = {
    101U, 1009U, 4093U, 8191U, 16381U, 32771U
};

struct GeneratedRun {
    bool pass = false;
    GenerationReport generation;
    PlanExecutionResult execution;
};

struct CountGate {
    int runs = 0;
    int passed = 0;
};

struct AuthorityGate {
    bool validAccepted = false;
    bool forgedRejected = false;
    bool nonMonotonicRejected = false;
    bool conflictRejectedAtomically = false;
};

struct SuiteSummary {
    CountGate graphPropagation;
    CountGate graphRepair;
    CountGate tapePropagation;
    CountGate tapeRepair;
    CountGate substitution;
    CountGate graphToTape;
    CountGate tapeToGraph;
    CountGate evidenceErasure;
    CountGate checkpointGraphToTape;
    CountGate checkpointTapeToGraph;

    int checkpointCorruptionRejected = 0;
    int checkpointDecoyRejected = 0;
    int destructionMutationCount = 0;
    int authorityExpansionEvents = 0;

    std::uint64_t generationEvaluations = 0U;
    std::uint64_t generationQueueOps = 0U;
    std::uint64_t generationMicros = 0U;

    std::size_t peakGeneratorScratch = 0U;
    std::size_t peakGraphPlanBytes = 0U;
    std::size_t peakTapePlanBytes = 0U;
    std::size_t maxCheckpointBytes = 0U;

    std::size_t generatedMachinerySerializedBytes = 0U;
    std::size_t authoritySerializedBytes = 0U;
    std::size_t coreDataProxy = 0U;

    bool coreDataWithinGate = false;
    bool checkpointWithinGate = false;
    bool primitiveCatalogueCarriesAuthority = false;
    bool hostPass = false;

    AuthorityGate authority;
    std::uint64_t suiteElapsedMicros = 0U;
};

const char* representationName(PlanRepresentation representation) {
    return representation == PlanRepresentation::Graph
        ? "graph"
        : "tape";
}

std::uint8_t payloadFor(std::uint32_t seed, int salt) {
    return static_cast<std::uint8_t>(
        (
            seed * 37U +
            static_cast<std::uint32_t>(salt * 19)
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

bool settleSetup(
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

Mesh makeStableBase(
    std::uint32_t seed,
    std::uint8_t payload
) {
    Mesh mesh(kWidth, kHeight, seed);

    if (
        publishVersion(
            mesh,
            static_cast<std::uint64_t>(seed) ^ 0x11001100ULL,
            1,
            payload
        )
    ) {
        settleSetup(mesh, seed ^ 0x22002200U);
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
    request.maxCycles = kMaxCycles;
    request.primitiveBudget = kPrimitiveBudget;
    request.evidence.memoryBudgetKnown = true;
    request.evidence.memoryBudgetBytes = kCoreDataGate;
    request.evidence.requestedRepresentationAvailable = true;
    request.evidence.actionabilityObservable = true;
    return request;
}

GeneratedRun generateAndRun(
    Mesh& mesh,
    PlanRepresentation representation,
    std::uint32_t seed
) {
    GeneratedRun run;
    ReconstructionGenerator generator;

    run.generation = generator.generate(
        mesh,
        requestFor(representation, seed)
    );

    if (!run.generation.ok) {
        return run;
    }

    if (representation == PlanRepresentation::Graph) {
        const GraphPlan plan =
            GraphPlan::fromRecipe(run.generation.recipe);

        run.execution = plan.execute(
            mesh,
            Schedule::Random,
            seed ^ 0x31415926U,
            kMaxCycles,
            kPrimitiveBudget
        );
    } else {
        const TapePlan plan =
            TapePlan::fromRecipe(run.generation.recipe);

        run.execution = plan.execute(
            mesh,
            Schedule::Random,
            seed ^ 0x27182818U,
            kMaxCycles,
            kPrimitiveBudget
        );
    }

    run.pass =
        run.execution.pass &&
        guaranteeSatisfied(measure(mesh));

    return run;
}

void absorbGeneration(
    SuiteSummary& summary,
    const GeneratedRun& run
) {
    summary.generationEvaluations +=
        run.generation.generationEvaluations;

    summary.generationQueueOps +=
        run.generation.generationQueueOps;

    summary.generationMicros +=
        run.generation.generationElapsedMicros;

    summary.peakGeneratorScratch = std::max(
        summary.peakGeneratorScratch,
        run.generation.generatorScratchBytes
    );

    if (
        run.generation.representation ==
            PlanRepresentation::Graph
    ) {
        summary.peakGraphPlanBytes = std::max(
            summary.peakGraphPlanBytes,
            run.execution.planBytes
        );
    } else {
        summary.peakTapePlanBytes = std::max(
            summary.peakTapePlanBytes,
            run.execution.planBytes
        );
    }
}

bool executeOneGeneratedStep(
    Mesh& mesh,
    const GenerationReport& generation,
    std::uint32_t seed
) {
    if (
        !generation.ok ||
        generation.recipe.steps.empty()
    ) {
        return false;
    }

    PrimitiveExecutor executor(mesh.size());
    Rng rng(seed);

    executor.execute(
        generation.recipe.steps.front(),
        mesh,
        Schedule::Random,
        rng,
        kPrimitiveBudget
    );

    executor.destroyGeneratedScratch();
    return true;
}

AuthorityGate runAuthorityGate() {
    AuthorityGate gate;

    Mesh mesh(kWidth, kHeight, 0xabcdef01U);
    ProtectedSourceBoundary boundary(0x123456789abcdef0ULL);

    const SourceCapability valid = boundary.issue();
    const SourceCapability secondValid = boundary.issue();
    const SourceCapability forged;

    const PublishResult accepted = boundary.publish(
        mesh,
        valid,
        PublishRequest{1, 0x42U}
    );

    gate.validAccepted =
        accepted.ok &&
        mesh.source().version == 1 &&
        mesh.source().data == 0x42U;

    const std::uint64_t afterValid = mesh.signature();

    const PublishResult forgedResult = boundary.publish(
        mesh,
        forged,
        PublishRequest{2, 0x43U}
    );

    gate.forgedRejected =
        !forgedResult.ok &&
        mesh.signature() == afterValid;

    const PublishResult nonMonotonic = boundary.publish(
        mesh,
        valid,
        PublishRequest{1, 0x44U}
    );

    gate.nonMonotonicRejected =
        !nonMonotonic.ok &&
        mesh.signature() == afterValid;

    const PublishResult conflict =
        boundary.publishPairAtomic(
            mesh,
            valid,
            PublishRequest{2, 0x55U},
            secondValid,
            PublishRequest{2, 0xaaU}
        );

    gate.conflictRejectedAtomically =
        !conflict.ok &&
        conflict.conflict &&
        mesh.signature() == afterValid;

    return gate;
}

bool authorityGatePass(const AuthorityGate& gate) {
    return
        gate.validAccepted &&
        gate.forgedRejected &&
        gate.nonMonotonicRejected &&
        gate.conflictRejectedAtomically;
}

void count(CountGate& gate, bool pass) {
    ++gate.runs;

    if (pass) {
        ++gate.passed;
    }
}

void runGraphTapeProofs(SuiteSummary& summary) {
    for (const std::uint32_t seed : kSeeds) {
        const std::uint8_t payload = payloadFor(seed, 1);
        const Mesh base = makeStableBase(seed, payload);

        {
            Mesh mesh = base;
            const std::uint8_t update =
                static_cast<std::uint8_t>(payload ^ 0xa5U);

            const bool published = publishVersion(
                mesh,
                static_cast<std::uint64_t>(seed) ^ 0x31000000ULL,
                2,
                update
            );

            const GeneratedRun run = generateAndRun(
                mesh,
                PlanRepresentation::Graph,
                seed ^ 0x31111111U
            );

            absorbGeneration(summary, run);
            count(
                summary.graphPropagation,
                published &&
                    run.pass &&
                    mesh.source().version == 2 &&
                    mesh.source().data == update
            );
        }

        {
            Mesh mesh = base;
            mesh.resetFraction(0.25, seed ^ 0x32000000U);

            const GeneratedRun run = generateAndRun(
                mesh,
                PlanRepresentation::Graph,
                seed ^ 0x32222222U
            );

            absorbGeneration(summary, run);
            count(summary.graphRepair, run.pass);
        }

        {
            Mesh mesh = base;
            const std::uint8_t update =
                static_cast<std::uint8_t>(payload ^ 0x5aU);

            const bool published = publishVersion(
                mesh,
                static_cast<std::uint64_t>(seed) ^ 0x33000000ULL,
                2,
                update
            );

            const GeneratedRun run = generateAndRun(
                mesh,
                PlanRepresentation::Tape,
                seed ^ 0x33333333U
            );

            absorbGeneration(summary, run);
            count(
                summary.tapePropagation,
                published &&
                    run.pass &&
                    mesh.source().version == 2 &&
                    mesh.source().data == update
            );
        }

        {
            Mesh mesh = base;
            mesh.resetFraction(0.25, seed ^ 0x34000000U);

            const GeneratedRun run = generateAndRun(
                mesh,
                PlanRepresentation::Tape,
                seed ^ 0x34444444U
            );

            absorbGeneration(summary, run);
            count(summary.tapeRepair, run.pass);
        }

        {
            Mesh graphMesh = base;
            Mesh tapeMesh = base;

            const std::uint8_t update =
                static_cast<std::uint8_t>(payload ^ 0x66U);

            const bool graphPublished = publishVersion(
                graphMesh,
                static_cast<std::uint64_t>(seed) ^ 0x35000000ULL,
                2,
                update
            );

            const bool tapePublished = publishVersion(
                tapeMesh,
                static_cast<std::uint64_t>(seed) ^ 0x35000000ULL,
                2,
                update
            );

            const GeneratedRun graphRun = generateAndRun(
                graphMesh,
                PlanRepresentation::Graph,
                seed ^ 0x35555555U
            );

            const GeneratedRun tapeRun = generateAndRun(
                tapeMesh,
                PlanRepresentation::Tape,
                seed ^ 0x35555555U
            );

            absorbGeneration(summary, graphRun);
            absorbGeneration(summary, tapeRun);

            count(
                summary.substitution,
                graphPublished &&
                    tapePublished &&
                    graphRun.pass &&
                    tapeRun.pass &&
                    graphMesh.source().version ==
                        tapeMesh.source().version &&
                    graphMesh.source().data ==
                        tapeMesh.source().data
            );
        }
    }
}

void runDestructionProofs(SuiteSummary& summary) {
    for (const std::uint32_t seed : kSeeds) {
        const std::uint8_t payload = payloadFor(seed, 2);

        {
            Mesh mesh = makeStableBase(seed, payload);

            const bool published = publishVersion(
                mesh,
                static_cast<std::uint64_t>(seed) ^ 0x41000000ULL,
                2,
                static_cast<std::uint8_t>(payload ^ 0x77U)
            );

            ReconstructionGenerator generator;
            GenerationReport first = generator.generate(
                mesh,
                requestFor(
                    PlanRepresentation::Graph,
                    seed ^ 0x41111111U
                )
            );

            const bool stepped = executeOneGeneratedStep(
                mesh,
                first,
                seed ^ 0x41222222U
            );

            first = GenerationReport{};
            const std::uint64_t before =
                mesh.signature();

            GenerationReport second = generator.generate(
                mesh,
                requestFor(
                    PlanRepresentation::Tape,
                    seed ^ 0x41333333U
                )
            );

            const std::uint64_t afterGeneration =
                mesh.signature();

            if (before != afterGeneration) {
                ++summary.destructionMutationCount;
            }

            GeneratedRun resumed;
            resumed.generation = second;

            if (second.ok) {
                const TapePlan plan =
                    TapePlan::fromRecipe(second.recipe);

                resumed.execution = plan.execute(
                    mesh,
                    Schedule::Random,
                    seed ^ 0x41444444U,
                    kMaxCycles,
                    kPrimitiveBudget
                );

                resumed.pass = resumed.execution.pass;
            }

            absorbGeneration(summary, resumed);

            count(
                summary.graphToTape,
                published &&
                    stepped &&
                    before == afterGeneration &&
                    resumed.pass
            );
        }

        {
            Mesh mesh = makeStableBase(
                seed ^ 0x50000000U,
                payload
            );

            const bool published = publishVersion(
                mesh,
                static_cast<std::uint64_t>(seed) ^ 0x42000000ULL,
                2,
                static_cast<std::uint8_t>(payload ^ 0x88U)
            );

            ReconstructionGenerator generator;
            GenerationReport first = generator.generate(
                mesh,
                requestFor(
                    PlanRepresentation::Tape,
                    seed ^ 0x42111111U
                )
            );

            const bool stepped = executeOneGeneratedStep(
                mesh,
                first,
                seed ^ 0x42222222U
            );

            first = GenerationReport{};
            const std::uint64_t before =
                mesh.signature();

            GenerationReport second = generator.generate(
                mesh,
                requestFor(
                    PlanRepresentation::Graph,
                    seed ^ 0x42333333U
                )
            );

            const std::uint64_t afterGeneration =
                mesh.signature();

            if (before != afterGeneration) {
                ++summary.destructionMutationCount;
            }

            GeneratedRun resumed;
            resumed.generation = second;

            if (second.ok) {
                const GraphPlan plan =
                    GraphPlan::fromRecipe(second.recipe);

                resumed.execution = plan.execute(
                    mesh,
                    Schedule::Random,
                    seed ^ 0x42444444U,
                    kMaxCycles,
                    kPrimitiveBudget
                );

                resumed.pass = resumed.execution.pass;
            }

            absorbGeneration(summary, resumed);

            count(
                summary.tapeToGraph,
                published &&
                    stepped &&
                    before == afterGeneration &&
                    resumed.pass
            );
        }
    }
}

void runEvidenceErasureProof(SuiteSummary& summary) {
    for (const std::uint32_t seed : kSeeds) {
        const std::uint8_t payload = payloadFor(seed, 3);
        Mesh mesh = makeStableBase(seed, payload);

        publishVersion(
            mesh,
            static_cast<std::uint64_t>(seed) ^ 0x51000000ULL,
            2,
            static_cast<std::uint8_t>(payload ^ 0x99U)
        );

        GenerationRequest request = requestFor(
            PlanRepresentation::Graph,
            seed ^ 0x51111111U
        );

        request.evidence.actionabilityObservable = false;

        ReconstructionGenerator generator;
        const GenerationReport report =
            generator.generate(mesh, request);

        bool executionPass = false;

        if (report.ok) {
            const GraphPlan plan =
                GraphPlan::fromRecipe(report.recipe);

            const PlanExecutionResult execution = plan.execute(
                mesh,
                Schedule::Random,
                seed ^ 0x51222222U,
                kMaxCycles,
                kPrimitiveBudget
            );

            executionPass = execution.pass;
        }

        count(
            summary.evidenceErasure,
            report.uncertain &&
                report.usedFallback &&
                executionPass
        );
    }
}

bool checkpointCrossRepresentation(
    SuiteSummary& summary,
    std::uint32_t seed,
    PlanRepresentation pre,
    PlanRepresentation post,
    bool damageCase
) {
    const std::uint8_t payload = payloadFor(seed, 4);
    Mesh mesh = makeStableBase(seed, payload);

    if (damageCase) {
        mesh.resetFraction(0.25, seed ^ 0x61000000U);
    } else {
        publishVersion(
            mesh,
            static_cast<std::uint64_t>(seed) ^ 0x61111111ULL,
            2,
            static_cast<std::uint8_t>(payload ^ 0xaaU)
        );
    }

    ReconstructionGenerator generator;
    GenerationReport generation = generator.generate(
        mesh,
        requestFor(pre, seed ^ 0x61222222U)
    );

    if (!generation.ok) {
        return false;
    }

    if (
        !executeOneGeneratedStep(
            mesh,
            generation,
            seed ^ 0x61333333U
        )
    ) {
        return false;
    }

    generation = GenerationReport{};

    CheckpointLedger ledger;
    const std::vector<std::uint8_t> bytes =
        encodeCheckpoint(mesh, &ledger);

    summary.maxCheckpointBytes = std::max(
        summary.maxCheckpointBytes,
        bytes.size()
    );

    summary.generatedMachinerySerializedBytes = std::max(
        summary.generatedMachinerySerializedBytes,
        ledger.generatedMachinerySerializedBytes
    );

    summary.authoritySerializedBytes = std::max(
        summary.authoritySerializedBytes,
        ledger.authoritySerializedBytes
    );

    const DecodedCheckpoint decoded =
        decodeCheckpoint(bytes);

    if (!decoded.ok) {
        return false;
    }

    Mesh restored(
        decoded.width,
        decoded.height,
        seed ^ 0x61444444U
    );

    if (!restoreCheckpoint(decoded, restored)) {
        return false;
    }

    const GeneratedRun resumed = generateAndRun(
        restored,
        post,
        seed ^ 0x61555555U
    );

    absorbGeneration(summary, resumed);

    if (!bytes.empty()) {
        std::vector<std::uint8_t> corrupted = bytes;
        corrupted[corrupted.size() / 2U] ^= 0x01U;

        if (!decodeCheckpoint(corrupted).ok) {
            ++summary.checkpointCorruptionRejected;
        }

        std::vector<std::uint8_t> decoy = bytes;
        decoy.push_back(0xdeU);
        decoy.push_back(0xadU);
        decoy.push_back(0xbeU);
        decoy.push_back(0xefU);

        if (!decodeCheckpoint(decoy).ok) {
            ++summary.checkpointDecoyRejected;
        }
    }

    return
        resumed.pass &&
        ledger.generatedMachinerySerializedBytes == 0U &&
        ledger.authoritySerializedBytes == 0U &&
        bytes.size() <= kCheckpointGate;
}

void runCheckpointProofs(SuiteSummary& summary) {
    for (const std::uint32_t seed : kSeeds) {
        count(
            summary.checkpointGraphToTape,
            checkpointCrossRepresentation(
                summary,
                seed,
                PlanRepresentation::Graph,
                PlanRepresentation::Tape,
                false
            )
        );

        count(
            summary.checkpointTapeToGraph,
            checkpointCrossRepresentation(
                summary,
                seed ^ 0x70000000U,
                PlanRepresentation::Tape,
                PlanRepresentation::Graph,
                true
            )
        );
    }
}

bool primitiveCatalogueAuthorityClean() {
    for (const PrimitiveDescriptor& descriptor :
         primitiveCatalogue()) {
        if (descriptor.carriesAuthority) {
            return false;
        }
    }

    return true;
}

bool gateExact(const CountGate& gate, int expected) {
    return
        gate.runs == expected &&
        gate.passed == expected;
}

SuiteSummary runSuite() {
    const auto started = std::chrono::steady_clock::now();

    SuiteSummary summary;

    runGraphTapeProofs(summary);
    runDestructionProofs(summary);
    runEvidenceErasureProof(summary);
    runCheckpointProofs(summary);

    summary.authority = runAuthorityGate();

    summary.primitiveCatalogueCarriesAuthority =
        !primitiveCatalogueAuthorityClean();

    Mesh resourceMesh(kWidth, kHeight, 1U);
    PrimitiveExecutor resourceExecutor(resourceMesh.size());

    summary.coreDataProxy =
        resourceMesh.dataBytesProxy() +
        resourceExecutor.scratchBytesProxy() +
        primitiveCatalogueBytesProxy() +
        summary.peakGeneratorScratch +
        summary.peakGraphPlanBytes +
        summary.peakTapePlanBytes;

    summary.coreDataWithinGate =
        summary.coreDataProxy <= kCoreDataGate;

    summary.checkpointWithinGate =
        summary.maxCheckpointBytes <= kCheckpointGate &&
        summary.generatedMachinerySerializedBytes == 0U &&
        summary.authoritySerializedBytes == 0U;

    summary.hostPass =
        gateExact(summary.graphPropagation, 6) &&
        gateExact(summary.graphRepair, 6) &&
        gateExact(summary.tapePropagation, 6) &&
        gateExact(summary.tapeRepair, 6) &&
        gateExact(summary.substitution, 6) &&
        gateExact(summary.graphToTape, 6) &&
        gateExact(summary.tapeToGraph, 6) &&
        gateExact(summary.evidenceErasure, 6) &&
        gateExact(summary.checkpointGraphToTape, 6) &&
        gateExact(summary.checkpointTapeToGraph, 6) &&
        summary.checkpointCorruptionRejected == 12 &&
        summary.checkpointDecoyRejected == 12 &&
        summary.destructionMutationCount == 0 &&
        summary.authorityExpansionEvents == 0 &&
        authorityGatePass(summary.authority) &&
        !summary.primitiveCatalogueCarriesAuthority &&
        summary.coreDataWithinGate &&
        summary.checkpointWithinGate;

    const auto ended = std::chrono::steady_clock::now();

    summary.suiteElapsedMicros =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<
                std::chrono::microseconds
            >(ended - started).count()
        );

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

std::string suiteJson(const SuiteSummary& summary) {
    std::ostringstream out;

    out << "{";
    out << "\"pass\":"
        << (summary.hostPass ? "true" : "false")
        << ",";
    out << "\"stageComplete\":false,";
    out << "\"scope\":\"host-and-in-process-n2\",";

    appendGate(out, "graphPropagation", summary.graphPropagation);
    out << ",";
    appendGate(out, "graphRepair", summary.graphRepair);
    out << ",";
    appendGate(out, "tapePropagation", summary.tapePropagation);
    out << ",";
    appendGate(out, "tapeRepair", summary.tapeRepair);
    out << ",";
    appendGate(out, "substitution", summary.substitution);
    out << ",";
    appendGate(out, "graphToTape", summary.graphToTape);
    out << ",";
    appendGate(out, "tapeToGraph", summary.tapeToGraph);
    out << ",";
    appendGate(out, "evidenceErasure", summary.evidenceErasure);
    out << ",";
    appendGate(
        out,
        "checkpointGraphToTape",
        summary.checkpointGraphToTape
    );
    out << ",";
    appendGate(
        out,
        "checkpointTapeToGraph",
        summary.checkpointTapeToGraph
    );
    out << ",";

    out << "\"checkpointAdversarial\":{";
    out << "\"corruptionRejected\":"
        << summary.checkpointCorruptionRejected << ",";
    out << "\"decoyRejected\":"
        << summary.checkpointDecoyRejected << ",";
    out << "\"maxBytes\":"
        << summary.maxCheckpointBytes << ",";
    out << "\"generatedMachinerySerializedBytes\":"
        << summary.generatedMachinerySerializedBytes << ",";
    out << "\"authoritySerializedBytes\":"
        << summary.authoritySerializedBytes;
    out << "},";

    out << "\"generation\":{";
    out << "\"evaluations\":"
        << summary.generationEvaluations << ",";
    out << "\"queueOps\":"
        << summary.generationQueueOps << ",";
    out << "\"elapsedMicros\":"
        << summary.generationMicros << ",";
    out << "\"peakScratchBytes\":"
        << summary.peakGeneratorScratch << ",";
    out << "\"primitiveCatalogueHash\":"
        << primitiveCatalogueHash();
    out << "},";

    out << "\"authority\":{";
    out << "\"validAccepted\":"
        << (summary.authority.validAccepted ? "true" : "false")
        << ",";
    out << "\"forgedRejected\":"
        << (summary.authority.forgedRejected ? "true" : "false")
        << ",";
    out << "\"nonMonotonicRejected\":"
        << (
            summary.authority.nonMonotonicRejected
                ? "true"
                : "false"
        )
        << ",";
    out << "\"conflictRejectedAtomically\":"
        << (
            summary.authority.conflictRejectedAtomically
                ? "true"
                : "false"
        )
        << ",";
    out << "\"primitiveCatalogueCarriesAuthority\":"
        << (
            summary.primitiveCatalogueCarriesAuthority
                ? "true"
                : "false"
        );
    out << "},";

    out << "\"resources\":{";
    out << "\"coreDataProxy\":"
        << summary.coreDataProxy << ",";
    out << "\"coreDataGate\":"
        << kCoreDataGate << ",";
    out << "\"withinCoreGate\":"
        << (
            summary.coreDataWithinGate
                ? "true"
                : "false"
        )
        << ",";
    out << "\"checkpointGate\":"
        << kCheckpointGate << ",";
    out << "\"withinCheckpointGate\":"
        << (
            summary.checkpointWithinGate
                ? "true"
                : "false"
        );
    out << "},";

    out << "\"antiCheat\":{";
    out << "\"generatedPlanPersisted\":false,";
    out << "\"queuePersisted\":false,";
    out << "\"specializationChoicePersisted\":false,";
    out << "\"authoritySecretPersisted\":false,";
    out << "\"targetFieldPersisted\":false,";
    out << "\"quickJs\":false,";
    out << "\"webView\":false";
    out << "},";

    out << "\"coldRestart\":{";
    out << "\"requiredForStageCompletion\":true,";
    out << "\"provenInThisRun\":false";
    out << "},";

    out << "\"suiteElapsedMicros\":"
        << summary.suiteElapsedMicros;
    out << "}";

    return out.str();
}

RunResult prepareCold(
    const std::string& path,
    PlanRepresentation preRepresentation,
    bool damageCase
) {
    constexpr std::uint32_t seed = 0x6d2a91f3U;

    const std::uint8_t payload = payloadFor(seed, 9);
    Mesh mesh = makeStableBase(seed, payload);

    if (damageCase) {
        mesh.resetFraction(0.25, seed ^ 0x81000000U);
    } else {
        publishVersion(
            mesh,
            0x8100112233445566ULL,
            2,
            static_cast<std::uint8_t>(payload ^ 0x5aU)
        );
    }

    ReconstructionGenerator generator;
    GenerationReport generation = generator.generate(
        mesh,
        requestFor(
            preRepresentation,
            seed ^ 0x81111111U
        )
    );

    if (!generation.ok) {
        return RunResult{
            false,
            "{\"pass\":false,\"reason\":\"generation-failed\"}"
        };
    }

    if (
        !executeOneGeneratedStep(
            mesh,
            generation,
            seed ^ 0x81222222U
        )
    ) {
        return RunResult{
            false,
            "{\"pass\":false,\"reason\":\"partial-step-failed\"}"
        };
    }

    generation = GenerationReport{};

    CheckpointLedger ledger;
    const std::vector<std::uint8_t> bytes =
        encodeCheckpoint(mesh, &ledger);

    const bool wrote =
        bytes.size() <= kCheckpointGate &&
        ledger.generatedMachinerySerializedBytes == 0U &&
        ledger.authoritySerializedBytes == 0U &&
        writeCheckpointFile(path, bytes);

    std::ostringstream out;
    out << "{";
    out << "\"pass\":" << (wrote ? "true" : "false") << ",";
    out << "\"phase\":\"prepared\",";
    out << "\"preRepresentation\":\""
        << representationName(preRepresentation)
        << "\",";
    out << "\"damageCase\":"
        << (damageCase ? "true" : "false")
        << ",";
    out << "\"checkpointBytes\":"
        << ledger.checkpointBytes << ",";
    out << "\"checkpointHash\":"
        << ledger.checkpointHash << ",";
    out << "\"generatedMachinerySerializedBytes\":0,";
    out << "\"authoritySerializedBytes\":0,";
    out << "\"processRestartRequired\":true";
    out << "}";

    return RunResult{wrote, out.str()};
}

RunResult resumeCold(
    const std::string& path,
    PlanRepresentation postRepresentation
) {
    const std::vector<std::uint8_t> bytes =
        readCheckpointFile(path);

    const DecodedCheckpoint decoded =
        decodeCheckpoint(bytes);

    if (!decoded.ok) {
        return RunResult{
            false,
            "{\"pass\":false,\"phase\":\"resume\","
            "\"reason\":\"checkpoint-invalid\"}"
        };
    }

    Mesh mesh(
        decoded.width,
        decoded.height,
        0x13579bdfU
    );

    if (!restoreCheckpoint(decoded, mesh)) {
        return RunResult{
            false,
            "{\"pass\":false,\"phase\":\"resume\","
            "\"reason\":\"restore-failed\"}"
        };
    }

    const GeneratedRun resumed = generateAndRun(
        mesh,
        postRepresentation,
        0x2468ace0U
    );

    const bool removed =
        resumed.pass &&
        removeCheckpointFile(path);

    std::ostringstream out;
    out << "{";
    out << "\"pass\":"
        << (
            resumed.pass && removed
                ? "true"
                : "false"
        )
        << ",";
    out << "\"phase\":\"resumed\",";
    out << "\"postRepresentation\":\""
        << representationName(postRepresentation)
        << "\",";
    out << "\"checkpointBytes\":"
        << bytes.size() << ",";
    out << "\"checkpointHash\":"
        << decoded.checkpointHash << ",";
    out << "\"guaranteePass\":"
        << (resumed.pass ? "true" : "false")
        << ",";
    out << "\"checkpointDeleted\":"
        << (removed ? "true" : "false")
        << ",";
    out << "\"externalProcessBoundaryEvidenceRequired\":true";
    out << "}";

    return RunResult{
        resumed.pass && removed,
        out.str()
    };
}

}  // namespace

RunResult runHostAndInProcessSuite() {
    const SuiteSummary summary = runSuite();

    return RunResult{
        summary.hostPass,
        suiteJson(summary)
    };
}

RunResult prepareColdCheckpointProof(
    const std::string& path,
    PlanRepresentation preRepresentation,
    bool damageCase
) {
    return prepareCold(
        path,
        preRepresentation,
        damageCase
    );
}

RunResult resumeColdCheckpointProof(
    const std::string& path,
    PlanRepresentation postRepresentation
) {
    return resumeCold(
        path,
        postRepresentation
    );
}

}  // namespace codynex::n2
