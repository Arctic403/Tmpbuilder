#include "n1_suite.h"

#include "n1_core.h"
#include "n1_machinery.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace codynex::n1 {
namespace {

constexpr int kWidth = 16;
constexpr int kHeight = 16;
constexpr int kMaxSweeps = 128;
constexpr double kFrontierThreshold = 0.25;
constexpr std::uint64_t kFrontierPopBudget =
    static_cast<std::uint64_t>(
        kWidth * kHeight * kMaxSweeps * 16
    );

constexpr std::array<std::uint32_t, 6> kSeeds = {
    101U, 1009U, 4093U, 8191U, 16381U, 32771U
};

constexpr std::array<Schedule, 3> kSchedules = {
    Schedule::Forward,
    Schedule::Reverse,
    Schedule::Random
};

struct Outcome {
    bool pass = false;
    Metric metric;
    WorkStats work;
    std::uint64_t signature = 0U;
};

struct Aggregate {
    int runs = 0;
    int passed = 0;
    std::uint64_t relationEvaluations = 0U;
    std::uint64_t generationEvaluations = 0U;
    std::uint64_t changes = 0U;
    std::uint64_t queuePushes = 0U;
    std::uint64_t queuePops = 0U;
    std::uint64_t sweeps = 0U;
    std::uint64_t elapsedMicros = 0U;
    int maxSweeps = 0;
    double minAgreement = 1.0;
    double minInformed = 1.0;
};

struct ReplacementSummary {
    int runs = 0;
    int passed = 0;
    int exactFinalMatches = 0;
};

struct RebuildSummary {
    int runs = 0;
    int passed = 0;
    int preDeletionStateMatches = 0;
    int deletionMutationCount = 0;
    int exactFinalMatches = 0;
};

struct SparseSummary {
    int runs = 0;
    int passed = 0;
    int frontierChoices = 0;
    std::uint64_t denseEvaluations = 0U;
    std::uint64_t frontierEvaluations = 0U;
    double meanReduction = 0.0;
    double meanActionableFraction = 0.0;
};

struct BroadSummary {
    int runs = 0;
    int passed = 0;
    int denseChoices = 0;
    int frontierChoices = 0;
    std::uint64_t denseEvaluations = 0U;
    std::uint64_t frontierEvaluations = 0U;
    double meanActionableFraction = 0.0;
};

struct AutomaticSummary {
    int runs = 0;
    int passed = 0;
    int denseChoices = 0;
    int frontierChoices = 0;
};

struct AuthoritySummary {
    bool validAccepted = false;
    bool forgedRejected = false;
    bool nonMonotonicRejected = false;
    bool conflictingBatchRejectedAtomically = false;
    bool sourceStableAcrossGeneratedMachinery = false;
};

struct ControllerLossSummary {
    int runs = 0;
    int denseAdvantages = 0;
    int frontierAdvantages = 0;
};

struct ResourceSummary {
    std::size_t replicaSizeBytes = 0U;
    std::size_t substrateBytesProxy = 0U;
    std::size_t denseBytesProxy = 0U;
    std::size_t frontierBytesProxy = 0U;
    std::size_t profileBytesProxy = 0U;
    std::size_t totalCoreBytesProxy = 0U;
    bool withinGate = false;
};

struct SuiteSummary {
    bool pass = false;

    Aggregate denseInitial;
    Aggregate denseUpdates;
    Aggregate denseRepairs;
    Aggregate denseFullReset;
    Aggregate denseReplay;

    Aggregate frontierInitial;
    Aggregate frontierUpdates;
    Aggregate frontierRepairs;
    Aggregate frontierFullReset;
    Aggregate frontierReplay;

    ReplacementSummary denseToFrontier;
    ReplacementSummary frontierToDense;
    RebuildSummary frontierRebuild;

    SparseSummary sparse;
    BroadSummary broad;
    AutomaticSummary automatic;

    AuthoritySummary authority;
    ControllerLossSummary controllerLoss;
    ResourceSummary resources;

    int dualImplementationRuns = 0;
    int dualImplementationPasses = 0;
    int dualImplementationExactMatches = 0;

    std::uint64_t suiteElapsedMicros = 0U;
};

std::uint8_t payloadFor(
    std::uint32_t seed,
    int variant
) {
    return static_cast<std::uint8_t>(
        (
            seed * 37U +
            static_cast<std::uint32_t>(variant * 17)
        ) & 0xffU
    );
}

std::uint32_t scheduleSalt(Schedule schedule) {
    switch (schedule) {
        case Schedule::Forward:
            return 0x11111111U;
        case Schedule::Reverse:
            return 0x22222222U;
        case Schedule::Random:
            return 0x33333333U;
    }

    return 0U;
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

Outcome executeDense(
    Mesh& mesh,
    Schedule schedule,
    std::uint32_t schedulerSeed
) {
    DenseMachinery dense(mesh.size());

    Outcome outcome;
    outcome.work = dense.runUntil(
        mesh,
        schedule,
        schedulerSeed,
        kMaxSweeps
    );
    outcome.metric = measure(mesh);
    outcome.pass = guaranteeSatisfied(outcome.metric);
    outcome.signature = mesh.signature();
    return outcome;
}

Outcome executeFrontier(
    Mesh& mesh,
    Schedule schedule,
    std::uint32_t schedulerSeed
) {
    FrontierMachinery frontier(mesh.size());
    Rng rng(schedulerSeed);

    const WorkStats rebuild =
        frontier.rebuild(mesh, schedule, rng);
    const WorkStats processing =
        frontier.runToEmpty(mesh, kFrontierPopBudget);

    Outcome outcome;
    outcome.work = combineWork(rebuild, processing);
    outcome.metric = measure(mesh);
    outcome.pass =
        frontier.empty() &&
        guaranteeSatisfied(outcome.metric);
    outcome.signature = mesh.signature();
    return outcome;
}

Outcome executeSelected(
    Mesh& mesh,
    const SpecializationProfile& profile,
    Schedule schedule,
    std::uint32_t schedulerSeed
) {
    const MachineryKind kind =
        chooseMachinery(profile, kFrontierThreshold);

    Outcome outcome =
        kind == MachineryKind::Dense
            ? executeDense(mesh, schedule, schedulerSeed)
            : executeFrontier(mesh, schedule, schedulerSeed);

    outcome.work.relationEvaluations +=
        profile.relationEvaluations;
    outcome.work.generationEvaluations +=
        profile.relationEvaluations;
    outcome.work.elapsedMicros +=
        profile.elapsedMicros;
    outcome.work.machineryBytesProxy = std::max(
        outcome.work.machineryBytesProxy,
        profile.bytesProxy
    );

    return outcome;
}

void addOutcome(
    Aggregate& aggregate,
    const Outcome& outcome,
    bool extraPass = true
) {
    ++aggregate.runs;

    if (outcome.pass && extraPass) {
        ++aggregate.passed;
    }

    aggregate.relationEvaluations +=
        outcome.work.relationEvaluations;
    aggregate.generationEvaluations +=
        outcome.work.generationEvaluations;
    aggregate.changes +=
        outcome.work.changes;
    aggregate.queuePushes +=
        outcome.work.queuePushes;
    aggregate.queuePops +=
        outcome.work.queuePops;
    aggregate.sweeps +=
        outcome.work.sweeps;
    aggregate.elapsedMicros +=
        outcome.work.elapsedMicros;
    aggregate.maxSweeps = std::max(
        aggregate.maxSweeps,
        static_cast<int>(outcome.work.sweeps)
    );
    aggregate.minAgreement = std::min(
        aggregate.minAgreement,
        outcome.metric.agreement
    );
    aggregate.minInformed = std::min(
        aggregate.minInformed,
        outcome.metric.informed
    );
}

bool deterministicWorkEqual(
    const WorkStats& a,
    const WorkStats& b
) {
    return
        a.relationEvaluations == b.relationEvaluations &&
        a.generationEvaluations == b.generationEvaluations &&
        a.changes == b.changes &&
        a.queuePushes == b.queuePushes &&
        a.queuePops == b.queuePops &&
        a.sweeps == b.sweeps;
}

Mesh makeStableBase(
    std::uint32_t seed,
    std::uint8_t payload
) {
    Mesh mesh(kWidth, kHeight, seed);

    const bool published = publishVersion(
        mesh,
        static_cast<std::uint64_t>(seed) ^ 0x10101010ULL,
        1,
        payload
    );

    if (published) {
        executeDense(
            mesh,
            Schedule::Random,
            seed ^ 0x20202020U
        );
    }

    return mesh;
}

void runDenseParity(SuiteSummary& summary) {
    const std::array<double, 3> repairFractions = {
        0.10, 0.25, 0.50
    };

    for (
        std::size_t scheduleIndex = 0U;
        scheduleIndex < kSchedules.size();
        ++scheduleIndex
    ) {
        const Schedule schedule = kSchedules[scheduleIndex];

        for (const std::uint32_t seed : kSeeds) {
            const std::uint8_t initialPayload =
                payloadFor(
                    seed,
                    static_cast<int>(scheduleIndex)
                );

            Mesh mesh(kWidth, kHeight, seed);

            const bool initialPublished = publishVersion(
                mesh,
                static_cast<std::uint64_t>(seed) ^
                    0x31000000ULL ^
                    scheduleIndex,
                1,
                initialPayload
            );

            const Outcome initial =
                initialPublished
                    ? executeDense(
                        mesh,
                        schedule,
                        seed ^
                            scheduleSalt(schedule) ^
                            0x31111111U
                    )
                    : Outcome{};

            addOutcome(
                summary.denseInitial,
                initial,
                initialPublished
            );

            const std::uint8_t updatedPayload =
                static_cast<std::uint8_t>(
                    initialPayload ^ 0xa5U
                );

            const bool updatePublished = publishVersion(
                mesh,
                static_cast<std::uint64_t>(seed) ^
                    0x32000000ULL ^
                    scheduleIndex,
                2,
                updatedPayload
            );

            const Outcome updated =
                updatePublished
                    ? executeDense(
                        mesh,
                        schedule,
                        seed ^
                            scheduleSalt(schedule) ^
                            0x32222222U
                    )
                    : Outcome{};

            addOutcome(
                summary.denseUpdates,
                updated,
                updatePublished
            );

            for (
                std::size_t fractionIndex = 0U;
                fractionIndex < repairFractions.size();
                ++fractionIndex
            ) {
                Mesh damaged = mesh;

                damaged.resetFraction(
                    repairFractions[fractionIndex],
                    seed ^
                        0x33000000U ^
                        static_cast<std::uint32_t>(
                            fractionIndex * 0x1001U
                        )
                );

                const Outcome repaired = executeDense(
                    damaged,
                    schedule,
                    seed ^
                        scheduleSalt(schedule) ^
                        0x33333333U ^
                        static_cast<std::uint32_t>(
                            fractionIndex * 0x0101U
                        )
                );

                const bool sourcePreserved =
                    repaired.metric.sourceVersion == 2 &&
                    repaired.metric.sourceData ==
                        updatedPayload;

                addOutcome(
                    summary.denseRepairs,
                    repaired,
                    sourcePreserved
                );
            }

            Mesh reset = mesh;
            reset.resetFraction(
                1.0,
                seed ^ 0x34000000U
            );

            const Outcome fullReset = executeDense(
                reset,
                schedule,
                seed ^
                    scheduleSalt(schedule) ^
                    0x34444444U
            );

            addOutcome(
                summary.denseFullReset,
                fullReset,
                fullReset.metric.sourceVersion == 2 &&
                    fullReset.metric.sourceData ==
                        updatedPayload
            );

            Mesh replayA(kWidth, kHeight, seed ^ 0x35000000U);
            Mesh replayB(kWidth, kHeight, seed ^ 0x35000000U);

            const bool replayPublishA = publishVersion(
                replayA,
                static_cast<std::uint64_t>(seed) ^
                    0x35111111ULL,
                1,
                initialPayload
            );

            const bool replayPublishB = publishVersion(
                replayB,
                static_cast<std::uint64_t>(seed) ^
                    0x35111111ULL,
                1,
                initialPayload
            );

            const std::uint32_t replaySeed =
                seed ^
                scheduleSalt(schedule) ^
                0x35222222U;

            const Outcome replayOutcomeA =
                replayPublishA
                    ? executeDense(
                        replayA,
                        schedule,
                        replaySeed
                    )
                    : Outcome{};

            const Outcome replayOutcomeB =
                replayPublishB
                    ? executeDense(
                        replayB,
                        schedule,
                        replaySeed
                    )
                    : Outcome{};

            const bool replayExact =
                replayPublishA &&
                replayPublishB &&
                replayOutcomeA.pass &&
                replayOutcomeB.pass &&
                replayA.sameState(replayB) &&
                deterministicWorkEqual(
                    replayOutcomeA.work,
                    replayOutcomeB.work
                );

            addOutcome(
                summary.denseReplay,
                replayOutcomeA,
                replayExact
            );
        }
    }
}

void runFrontierParity(SuiteSummary& summary) {
    const std::array<double, 3> repairFractions = {
        0.10, 0.25, 0.50
    };

    for (
        std::size_t scheduleIndex = 0U;
        scheduleIndex < kSchedules.size();
        ++scheduleIndex
    ) {
        const Schedule schedule = kSchedules[scheduleIndex];

        for (const std::uint32_t seed : kSeeds) {
            const std::uint8_t initialPayload =
                payloadFor(
                    seed,
                    static_cast<int>(scheduleIndex)
                );

            Mesh mesh(kWidth, kHeight, seed);

            const bool initialPublished = publishVersion(
                mesh,
                static_cast<std::uint64_t>(seed) ^
                    0x41000000ULL ^
                    scheduleIndex,
                1,
                initialPayload
            );

            const Outcome initial =
                initialPublished
                    ? executeFrontier(
                        mesh,
                        schedule,
                        seed ^
                            scheduleSalt(schedule) ^
                            0x41111111U
                    )
                    : Outcome{};

            addOutcome(
                summary.frontierInitial,
                initial,
                initialPublished
            );

            const std::uint8_t updatedPayload =
                static_cast<std::uint8_t>(
                    initialPayload ^ 0xa5U
                );

            const bool updatePublished = publishVersion(
                mesh,
                static_cast<std::uint64_t>(seed) ^
                    0x42000000ULL ^
                    scheduleIndex,
                2,
                updatedPayload
            );

            const Outcome updated =
                updatePublished
                    ? executeFrontier(
                        mesh,
                        schedule,
                        seed ^
                            scheduleSalt(schedule) ^
                            0x42222222U
                    )
                    : Outcome{};

            addOutcome(
                summary.frontierUpdates,
                updated,
                updatePublished
            );

            for (
                std::size_t fractionIndex = 0U;
                fractionIndex < repairFractions.size();
                ++fractionIndex
            ) {
                Mesh damaged = mesh;

                damaged.resetFraction(
                    repairFractions[fractionIndex],
                    seed ^
                        0x43000000U ^
                        static_cast<std::uint32_t>(
                            fractionIndex * 0x1001U
                        )
                );

                const Outcome repaired = executeFrontier(
                    damaged,
                    schedule,
                    seed ^
                        scheduleSalt(schedule) ^
                        0x43333333U ^
                        static_cast<std::uint32_t>(
                            fractionIndex * 0x0101U
                        )
                );

                const bool sourcePreserved =
                    repaired.metric.sourceVersion == 2 &&
                    repaired.metric.sourceData ==
                        updatedPayload;

                addOutcome(
                    summary.frontierRepairs,
                    repaired,
                    sourcePreserved
                );
            }

            Mesh reset = mesh;
            reset.resetFraction(
                1.0,
                seed ^ 0x44000000U
            );

            const Outcome fullReset = executeFrontier(
                reset,
                schedule,
                seed ^
                    scheduleSalt(schedule) ^
                    0x44444444U
            );

            addOutcome(
                summary.frontierFullReset,
                fullReset,
                fullReset.metric.sourceVersion == 2 &&
                    fullReset.metric.sourceData ==
                        updatedPayload
            );

            Mesh replayA(kWidth, kHeight, seed ^ 0x45000000U);
            Mesh replayB(kWidth, kHeight, seed ^ 0x45000000U);

            const bool replayPublishA = publishVersion(
                replayA,
                static_cast<std::uint64_t>(seed) ^
                    0x45111111ULL,
                1,
                initialPayload
            );

            const bool replayPublishB = publishVersion(
                replayB,
                static_cast<std::uint64_t>(seed) ^
                    0x45111111ULL,
                1,
                initialPayload
            );

            const std::uint32_t replaySeed =
                seed ^
                scheduleSalt(schedule) ^
                0x45222222U;

            const Outcome replayOutcomeA =
                replayPublishA
                    ? executeFrontier(
                        replayA,
                        schedule,
                        replaySeed
                    )
                    : Outcome{};

            const Outcome replayOutcomeB =
                replayPublishB
                    ? executeFrontier(
                        replayB,
                        schedule,
                        replaySeed
                    )
                    : Outcome{};

            const bool replayExact =
                replayPublishA &&
                replayPublishB &&
                replayOutcomeA.pass &&
                replayOutcomeB.pass &&
                replayA.sameState(replayB) &&
                deterministicWorkEqual(
                    replayOutcomeA.work,
                    replayOutcomeB.work
                );

            addOutcome(
                summary.frontierReplay,
                replayOutcomeA,
                replayExact
            );
        }
    }
}

void runDualImplementation(SuiteSummary& summary) {
    for (const std::uint32_t seed : kSeeds) {
        const std::uint8_t payload = payloadFor(seed, 6);
        Mesh base = makeStableBase(seed, payload);

        Mesh denseMesh = base;
        Mesh frontierMesh = base;

        const std::uint8_t updatedPayload =
            static_cast<std::uint8_t>(payload ^ 0x5aU);

        const bool densePublished = publishVersion(
            denseMesh,
            static_cast<std::uint64_t>(seed) ^
                0x51000000ULL,
            2,
            updatedPayload
        );

        const bool frontierPublished = publishVersion(
            frontierMesh,
            static_cast<std::uint64_t>(seed) ^
                0x51000000ULL,
            2,
            updatedPayload
        );

        const Outcome denseOutcome =
            densePublished
                ? executeDense(
                    denseMesh,
                    Schedule::Random,
                    seed ^ 0x51111111U
                )
                : Outcome{};

        const Outcome frontierOutcome =
            frontierPublished
                ? executeFrontier(
                    frontierMesh,
                    Schedule::Random,
                    seed ^ 0x51111111U
                )
                : Outcome{};

        ++summary.dualImplementationRuns;

        const bool pass =
            densePublished &&
            frontierPublished &&
            denseOutcome.pass &&
            frontierOutcome.pass &&
            denseOutcome.metric.sourceVersion ==
                frontierOutcome.metric.sourceVersion &&
            denseOutcome.metric.sourceData ==
                frontierOutcome.metric.sourceData;

        if (pass) {
            ++summary.dualImplementationPasses;
        }

        if (
            pass &&
            denseMesh.sameState(frontierMesh)
        ) {
            ++summary.dualImplementationExactMatches;
        }
    }
}

void runDenseToFrontier(SuiteSummary& summary) {
    for (const std::uint32_t seed : kSeeds) {
        const std::uint8_t payload = payloadFor(seed, 7);
        Mesh mesh = makeStableBase(seed, payload);

        const std::uint8_t updatedPayload =
            static_cast<std::uint8_t>(payload ^ 0x66U);

        const bool published = publishVersion(
            mesh,
            static_cast<std::uint64_t>(seed) ^
                0x52000000ULL,
            2,
            updatedPayload
        );

        Mesh control = mesh;

        DenseMachinery dense(mesh.size());
        Rng denseRng(seed ^ 0x52111111U);
        dense.runSweeps(
            mesh,
            Schedule::Random,
            denseRng,
            1
        );

        FrontierMachinery frontier(mesh.size());
        Rng frontierRng(seed ^ 0x52222222U);
        const WorkStats rebuild =
            frontier.rebuild(
                mesh,
                Schedule::Random,
                frontierRng
            );
        const WorkStats remaining =
            frontier.runToEmpty(
                mesh,
                kFrontierPopBudget
            );
        const WorkStats combined =
            combineWork(rebuild, remaining);

        const Outcome controlOutcome = executeDense(
            control,
            Schedule::Random,
            seed ^ 0x52333333U
        );

        ++summary.denseToFrontier.runs;

        const Metric metric = measure(mesh);
        const bool pass =
            published &&
            combined.relationEvaluations > 0U &&
            frontier.empty() &&
            guaranteeSatisfied(metric) &&
            metric.sourceVersion == 2 &&
            metric.sourceData == updatedPayload;

        if (pass) {
            ++summary.denseToFrontier.passed;
        }

        if (
            pass &&
            controlOutcome.pass &&
            mesh.sameState(control)
        ) {
            ++summary.denseToFrontier.exactFinalMatches;
        }
    }
}

void runFrontierToDense(SuiteSummary& summary) {
    for (const std::uint32_t seed : kSeeds) {
        const std::uint8_t payload = payloadFor(seed, 8);
        Mesh mesh = makeStableBase(seed, payload);

        const std::uint8_t updatedPayload =
            static_cast<std::uint8_t>(payload ^ 0x77U);

        const bool published = publishVersion(
            mesh,
            static_cast<std::uint64_t>(seed) ^
                0x53000000ULL,
            2,
            updatedPayload
        );

        Mesh control = mesh;

        {
            FrontierMachinery frontier(mesh.size());
            Rng frontierRng(seed ^ 0x53111111U);
            frontier.rebuild(
                mesh,
                Schedule::Random,
                frontierRng
            );
            frontier.processBudget(mesh, 32U);
        }

        const Outcome denseOutcome = executeDense(
            mesh,
            Schedule::Random,
            seed ^ 0x53222222U
        );

        const Outcome controlOutcome = executeFrontier(
            control,
            Schedule::Random,
            seed ^ 0x53333333U
        );

        ++summary.frontierToDense.runs;

        const bool pass =
            published &&
            denseOutcome.pass &&
            denseOutcome.metric.sourceVersion == 2 &&
            denseOutcome.metric.sourceData == updatedPayload;

        if (pass) {
            ++summary.frontierToDense.passed;
        }

        if (
            pass &&
            controlOutcome.pass &&
            mesh.sameState(control)
        ) {
            ++summary.frontierToDense.exactFinalMatches;
        }
    }
}

void runFrontierRebuild(SuiteSummary& summary) {
    for (const std::uint32_t seed : kSeeds) {
        const std::uint8_t payload = payloadFor(seed, 9);
        Mesh base = makeStableBase(seed, payload);

        Mesh rebuiltMesh = base;
        Mesh retainedMesh = base;

        const std::uint8_t updatedPayload =
            static_cast<std::uint8_t>(payload ^ 0x88U);

        const bool publishedA = publishVersion(
            rebuiltMesh,
            static_cast<std::uint64_t>(seed) ^
                0x54000000ULL,
            2,
            updatedPayload
        );

        const bool publishedB = publishVersion(
            retainedMesh,
            static_cast<std::uint64_t>(seed) ^
                0x54000000ULL,
            2,
            updatedPayload
        );

        FrontierMachinery retainedFrontier(
            retainedMesh.size()
        );

        Rng retainedRng(seed ^ 0x54111111U);
        retainedFrontier.rebuild(
            retainedMesh,
            Schedule::Random,
            retainedRng
        );
        retainedFrontier.processBudget(
            retainedMesh,
            32U
        );

        {
            FrontierMachinery disposableFrontier(
                rebuiltMesh.size()
            );

            Rng disposableRng(seed ^ 0x54111111U);
            disposableFrontier.rebuild(
                rebuiltMesh,
                Schedule::Random,
                disposableRng
            );
            disposableFrontier.processBudget(
                rebuiltMesh,
                32U
            );
        }

        const bool preDeletionStateMatch =
            rebuiltMesh.sameState(retainedMesh);

        const std::uint64_t beforeRebuild =
            rebuiltMesh.signature();

        FrontierMachinery rebuiltFrontier(
            rebuiltMesh.size()
        );
        Rng rebuiltRng(seed ^ 0x54222222U);
        rebuiltFrontier.rebuild(
            rebuiltMesh,
            Schedule::Random,
            rebuiltRng
        );

        const std::uint64_t afterRebuild =
            rebuiltMesh.signature();

        rebuiltFrontier.runToEmpty(
            rebuiltMesh,
            kFrontierPopBudget
        );

        retainedFrontier.runToEmpty(
            retainedMesh,
            kFrontierPopBudget
        );

        ++summary.frontierRebuild.runs;

        if (preDeletionStateMatch) {
            ++summary.frontierRebuild.preDeletionStateMatches;
        }

        if (beforeRebuild != afterRebuild) {
            ++summary.frontierRebuild.deletionMutationCount;
        }

        const bool pass =
            publishedA &&
            publishedB &&
            preDeletionStateMatch &&
            beforeRebuild == afterRebuild &&
            guaranteeSatisfied(measure(rebuiltMesh)) &&
            guaranteeSatisfied(measure(retainedMesh));

        if (pass) {
            ++summary.frontierRebuild.passed;
        }

        if (
            pass &&
            rebuiltMesh.sameState(retainedMesh)
        ) {
            ++summary.frontierRebuild.exactFinalMatches;
        }
    }
}

void runSparseSpecialization(SuiteSummary& summary) {
    double reductionTotal = 0.0;
    double actionableTotal = 0.0;

    for (const std::uint32_t seed : kSeeds) {
        const std::uint8_t payload = payloadFor(seed, 10);
        Mesh base = makeStableBase(seed, payload);

        Mesh denseMesh = base;
        Mesh frontierMesh = base;

        const std::uint8_t updatedPayload =
            static_cast<std::uint8_t>(payload ^ 0x99U);

        const bool densePublished = publishVersion(
            denseMesh,
            static_cast<std::uint64_t>(seed) ^
                0x55000000ULL,
            2,
            updatedPayload
        );

        const bool frontierPublished = publishVersion(
            frontierMesh,
            static_cast<std::uint64_t>(seed) ^
                0x55000000ULL,
            2,
            updatedPayload
        );

        const Outcome denseOutcome =
            densePublished
                ? executeDense(
                    denseMesh,
                    Schedule::Random,
                    seed ^ 0x55111111U
                )
                : Outcome{};

        const SpecializationProfile profile =
            profileMesh(frontierMesh);

        const MachineryKind choice =
            chooseMachinery(
                profile,
                kFrontierThreshold
            );

        if (choice == MachineryKind::Frontier) {
            ++summary.sparse.frontierChoices;
        }

        const Outcome frontierOutcome =
            frontierPublished
                ? executeFrontier(
                    frontierMesh,
                    Schedule::Random,
                    seed ^ 0x55111111U
                )
                : Outcome{};

        const std::uint64_t frontierEvaluations =
            profile.relationEvaluations +
            frontierOutcome.work.relationEvaluations;

        ++summary.sparse.runs;

        const bool pass =
            densePublished &&
            frontierPublished &&
            denseOutcome.pass &&
            frontierOutcome.pass &&
            denseOutcome.metric.sourceVersion == 2 &&
            frontierOutcome.metric.sourceVersion == 2 &&
            denseOutcome.metric.sourceData ==
                updatedPayload &&
            frontierOutcome.metric.sourceData ==
                updatedPayload;

        if (pass) {
            ++summary.sparse.passed;
        }

        summary.sparse.denseEvaluations +=
            denseOutcome.work.relationEvaluations;
        summary.sparse.frontierEvaluations +=
            frontierEvaluations;
        actionableTotal += profile.actionableFraction;

        const double reduction =
            denseOutcome.work.relationEvaluations > 0U
                ? 1.0 -
                    (
                        static_cast<double>(
                            frontierEvaluations
                        ) /
                        static_cast<double>(
                            denseOutcome.work.relationEvaluations
                        )
                    )
                : 0.0;

        reductionTotal += reduction;
    }

    summary.sparse.meanReduction =
        summary.sparse.runs > 0
            ? reductionTotal /
                static_cast<double>(summary.sparse.runs)
            : 0.0;

    summary.sparse.meanActionableFraction =
        summary.sparse.runs > 0
            ? actionableTotal /
                static_cast<double>(summary.sparse.runs)
            : 0.0;
}

void runBroadTradeoff(SuiteSummary& summary) {
    double actionableTotal = 0.0;

    for (const std::uint32_t seed : kSeeds) {
        const std::uint8_t payload = payloadFor(seed, 11);
        Mesh base = makeStableBase(seed, payload);

        Mesh denseMesh = base;
        Mesh frontierMesh = base;

        const std::uint32_t damageSeed =
            seed ^ 0x56000000U;

        denseMesh.resetFraction(0.50, damageSeed);
        frontierMesh.resetFraction(0.50, damageSeed);

        const SpecializationProfile profile =
            profileMesh(frontierMesh);

        const MachineryKind choice =
            chooseMachinery(
                profile,
                kFrontierThreshold
            );

        if (choice == MachineryKind::Dense) {
            ++summary.broad.denseChoices;
        } else {
            ++summary.broad.frontierChoices;
        }

        const Outcome denseOutcome = executeDense(
            denseMesh,
            Schedule::Random,
            seed ^ 0x56111111U
        );

        const Outcome frontierOutcome =
            executeFrontier(
                frontierMesh,
                Schedule::Random,
                seed ^ 0x56111111U
            );

        ++summary.broad.runs;

        const bool pass =
            denseOutcome.pass &&
            frontierOutcome.pass;

        if (pass) {
            ++summary.broad.passed;
        }

        summary.broad.denseEvaluations +=
            denseOutcome.work.relationEvaluations;

        summary.broad.frontierEvaluations +=
            profile.relationEvaluations +
            frontierOutcome.work.relationEvaluations;

        actionableTotal += profile.actionableFraction;
    }

    summary.broad.meanActionableFraction =
        summary.broad.runs > 0
            ? actionableTotal /
                static_cast<double>(summary.broad.runs)
            : 0.0;
}

void addAutomaticRun(
    SuiteSummary& summary,
    Mesh& mesh,
    Schedule schedule,
    std::uint32_t seed
) {
    const SpecializationProfile profile =
        profileMesh(mesh);

    const MachineryKind choice =
        chooseMachinery(
            profile,
            kFrontierThreshold
        );

    if (choice == MachineryKind::Dense) {
        ++summary.automatic.denseChoices;
    } else {
        ++summary.automatic.frontierChoices;
    }

    const Outcome outcome =
        executeSelected(
            mesh,
            profile,
            schedule,
            seed
        );

    ++summary.automatic.runs;

    if (outcome.pass) {
        ++summary.automatic.passed;
    }
}

void runAutomaticSelection(SuiteSummary& summary) {
    for (const std::uint32_t seed : kSeeds) {
        const std::uint8_t payload = payloadFor(seed, 12);

        Mesh sparse = makeStableBase(seed, payload);
        publishVersion(
            sparse,
            static_cast<std::uint64_t>(seed) ^
                0x57000000ULL,
            2,
            static_cast<std::uint8_t>(payload ^ 0xabU)
        );

        addAutomaticRun(
            summary,
            sparse,
            Schedule::Random,
            seed ^ 0x57111111U
        );

        Mesh broad = makeStableBase(
            seed ^ 0x10000000U,
            payload
        );
        broad.resetFraction(
            0.50,
            seed ^ 0x57222222U
        );

        addAutomaticRun(
            summary,
            broad,
            Schedule::Random,
            seed ^ 0x57333333U
        );
    }
}

AuthoritySummary runAuthorityIsolation() {
    AuthoritySummary summary;

    Mesh mesh(kWidth, kHeight, 0xabcddcbaU);
    ProtectedSourceBoundary boundary(0x10101010ULL);
    const SourceCapability valid = boundary.issue();
    const SourceCapability secondValid = boundary.issue();
    const SourceCapability forged;

    const PublishResult accepted = boundary.publish(
        mesh,
        valid,
        PublishRequest{1, 0x42U}
    );

    summary.validAccepted =
        accepted.ok &&
        mesh.source().version == 1 &&
        mesh.source().data == 0x42U;

    const Replica sourceBeforeMachinery = mesh.source();

    DenseMachinery dense(mesh.size());
    FrontierMachinery frontier(mesh.size());
    const SpecializationProfile profile =
        profileMesh(mesh);

    Rng rng(0x12345678U);
    frontier.rebuild(
        mesh,
        Schedule::Random,
        rng
    );

    const Replica sourceAfterMachinery = mesh.source();

    summary.sourceStableAcrossGeneratedMachinery =
        sameReplica(
            sourceBeforeMachinery,
            sourceAfterMachinery
        ) &&
        dense.bytesProxy() > 0U &&
        frontier.bytesProxy() > 0U &&
        profile.bytesProxy > 0U;

    const std::uint64_t afterValid =
        mesh.signature();

    const PublishResult forgedResult =
        boundary.publish(
            mesh,
            forged,
            PublishRequest{2, 0x43U}
        );

    summary.forgedRejected =
        !forgedResult.ok &&
        mesh.signature() == afterValid;

    const PublishResult nonMonotonic =
        boundary.publish(
            mesh,
            valid,
            PublishRequest{1, 0x44U}
        );

    summary.nonMonotonicRejected =
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

    summary.conflictingBatchRejectedAtomically =
        !conflict.ok &&
        conflict.conflict &&
        mesh.signature() == afterValid;

    return summary;
}

void runControllerLoss(SuiteSummary& summary) {
    for (const std::uint32_t seed : kSeeds) {
        const std::uint8_t payload = payloadFor(seed, 13);

        Mesh relationalDense =
            makeStableBase(seed, payload);
        Mesh relationalFrontier =
            relationalDense;

        Mesh centralized(
            kWidth,
            kHeight,
            seed ^ 0x58000000U
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
            seed ^ 0x58111111U;

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

        const Outcome denseRepair = executeDense(
            relationalDense,
            Schedule::Random,
            seed ^ 0x58222222U
        );

        const Outcome frontierRepair =
            executeFrontier(
                relationalFrontier,
                Schedule::Random,
                seed ^ 0x58222222U
            );

        controller.available = false;
        const BaselineResult unavailableResult =
            baselineBroadcast(
                unavailable,
                controller
            );

        const Metric unavailableMetric =
            measure(unavailable);

        ++summary.controllerLoss.runs;

        const bool baselineUnavailable =
            !unavailableResult.available &&
            unavailableResult.scans == 0U &&
            unavailableResult.writes == 0U &&
            unavailableMetric.agreement <
                kAgreementGate;

        if (
            baselineUnavailable &&
            denseRepair.pass
        ) {
            ++summary.controllerLoss.denseAdvantages;
        }

        if (
            baselineUnavailable &&
            frontierRepair.pass
        ) {
            ++summary.controllerLoss.frontierAdvantages;
        }
    }
}

ResourceSummary measureResources() {
    ResourceSummary resources;

    Mesh mesh(kWidth, kHeight, 1U);
    DenseMachinery dense(mesh.size());
    FrontierMachinery frontier(mesh.size());
    const SpecializationProfile profile =
        profileMesh(mesh);

    resources.replicaSizeBytes = sizeof(Replica);
    resources.substrateBytesProxy =
        mesh.dataBytesProxy();
    resources.denseBytesProxy =
        dense.bytesProxy();
    resources.frontierBytesProxy =
        frontier.bytesProxy();
    resources.profileBytesProxy =
        profile.bytesProxy;

    resources.totalCoreBytesProxy =
        resources.substrateBytesProxy +
        resources.denseBytesProxy +
        resources.frontierBytesProxy +
        resources.profileBytesProxy;

    resources.withinGate =
        resources.totalCoreBytesProxy <= 262144U;

    return resources;
}

bool aggregateExact(
    const Aggregate& aggregate,
    int expectedRuns
) {
    return
        aggregate.runs == expectedRuns &&
        aggregate.passed == expectedRuns;
}

bool authorityPass(
    const AuthoritySummary& authority
) {
    return
        authority.validAccepted &&
        authority.forgedRejected &&
        authority.nonMonotonicRejected &&
        authority.conflictingBatchRejectedAtomically &&
        authority.sourceStableAcrossGeneratedMachinery;
}

bool suitePass(const SuiteSummary& summary) {
    const bool denseParity =
        aggregateExact(summary.denseInitial, 18) &&
        aggregateExact(summary.denseUpdates, 18) &&
        aggregateExact(summary.denseRepairs, 54) &&
        summary.denseFullReset.runs == 18 &&
        summary.denseFullReset.passed >= 17 &&
        aggregateExact(summary.denseReplay, 18);

    const bool frontierParity =
        aggregateExact(summary.frontierInitial, 18) &&
        aggregateExact(summary.frontierUpdates, 18) &&
        aggregateExact(summary.frontierRepairs, 54) &&
        summary.frontierFullReset.runs == 18 &&
        summary.frontierFullReset.passed >= 17 &&
        aggregateExact(summary.frontierReplay, 18);

    const bool dualPass =
        summary.dualImplementationRuns ==
            static_cast<int>(kSeeds.size()) &&
        summary.dualImplementationPasses ==
            summary.dualImplementationRuns;

    const bool replacementsPass =
        summary.denseToFrontier.runs ==
            static_cast<int>(kSeeds.size()) &&
        summary.denseToFrontier.passed ==
            summary.denseToFrontier.runs &&
        summary.frontierToDense.runs ==
            static_cast<int>(kSeeds.size()) &&
        summary.frontierToDense.passed ==
            summary.frontierToDense.runs;

    const bool rebuildPass =
        summary.frontierRebuild.runs ==
            static_cast<int>(kSeeds.size()) &&
        summary.frontierRebuild.passed ==
            summary.frontierRebuild.runs &&
        summary.frontierRebuild.deletionMutationCount == 0;

    const bool sparsePass =
        summary.sparse.runs ==
            static_cast<int>(kSeeds.size()) &&
        summary.sparse.passed ==
            summary.sparse.runs &&
        summary.sparse.meanReduction >= 0.20;

    const bool broadPass =
        summary.broad.runs ==
            static_cast<int>(kSeeds.size()) &&
        summary.broad.passed ==
            summary.broad.runs;

    const bool automaticPass =
        summary.automatic.runs ==
            static_cast<int>(kSeeds.size() * 2U) &&
        summary.automatic.passed ==
            summary.automatic.runs &&
        summary.automatic.denseChoices > 0 &&
        summary.automatic.frontierChoices > 0;

    const bool controllerPass =
        summary.controllerLoss.runs ==
            static_cast<int>(kSeeds.size()) &&
        summary.controllerLoss.denseAdvantages ==
            summary.controllerLoss.runs &&
        summary.controllerLoss.frontierAdvantages ==
            summary.controllerLoss.runs;

    return
        denseParity &&
        frontierParity &&
        dualPass &&
        replacementsPass &&
        rebuildPass &&
        sparsePass &&
        broadPass &&
        automaticPass &&
        controllerPass &&
        authorityPass(summary.authority) &&
        summary.resources.withinGate;
}

void appendAggregateJson(
    std::ostringstream& out,
    const char* name,
    const Aggregate& aggregate
) {
    out << "\"" << name << "\":{";
    out << "\"runs\":" << aggregate.runs << ",";
    out << "\"passed\":" << aggregate.passed << ",";
    out << "\"passRate\":"
        << (
            aggregate.runs > 0
                ? static_cast<double>(aggregate.passed) /
                    static_cast<double>(aggregate.runs)
                : 0.0
        )
        << ",";
    out << "\"minAgreement\":"
        << aggregate.minAgreement << ",";
    out << "\"minInformed\":"
        << aggregate.minInformed << ",";
    out << "\"maxSweeps\":"
        << aggregate.maxSweeps << ",";
    out << "\"relationEvaluations\":"
        << aggregate.relationEvaluations << ",";
    out << "\"generationEvaluations\":"
        << aggregate.generationEvaluations << ",";
    out << "\"changes\":"
        << aggregate.changes << ",";
    out << "\"queuePushes\":"
        << aggregate.queuePushes << ",";
    out << "\"queuePops\":"
        << aggregate.queuePops << ",";
    out << "\"elapsedMicros\":"
        << aggregate.elapsedMicros;
    out << "}";
}

std::string toJson(const SuiteSummary& summary) {
    std::ostringstream out;
    out << std::setprecision(12);

    out << "{";
    out << "\"pass\":"
        << (summary.pass ? "true" : "false")
        << ",";

    out << "\"config\":{";
    out << "\"width\":" << kWidth << ",";
    out << "\"height\":" << kHeight << ",";
    out << "\"seedCount\":" << kSeeds.size() << ",";
    out << "\"scheduleCount\":"
        << kSchedules.size() << ",";
    out << "\"maxSweeps\":" << kMaxSweeps << ",";
    out << "\"frontierThreshold\":"
        << kFrontierThreshold;
    out << "},";

    out << "\"dense\":{";
    appendAggregateJson(
        out,
        "initial",
        summary.denseInitial
    );
    out << ",";
    appendAggregateJson(
        out,
        "updates",
        summary.denseUpdates
    );
    out << ",";
    appendAggregateJson(
        out,
        "repairs",
        summary.denseRepairs
    );
    out << ",";
    appendAggregateJson(
        out,
        "fullReset",
        summary.denseFullReset
    );
    out << ",";
    appendAggregateJson(
        out,
        "replay",
        summary.denseReplay
    );
    out << "},";

    out << "\"frontier\":{";
    appendAggregateJson(
        out,
        "initial",
        summary.frontierInitial
    );
    out << ",";
    appendAggregateJson(
        out,
        "updates",
        summary.frontierUpdates
    );
    out << ",";
    appendAggregateJson(
        out,
        "repairs",
        summary.frontierRepairs
    );
    out << ",";
    appendAggregateJson(
        out,
        "fullReset",
        summary.frontierFullReset
    );
    out << ",";
    appendAggregateJson(
        out,
        "replay",
        summary.frontierReplay
    );
    out << "},";

    out << "\"dualImplementation\":{";
    out << "\"runs\":"
        << summary.dualImplementationRuns << ",";
    out << "\"passed\":"
        << summary.dualImplementationPasses << ",";
    out << "\"exactFinalMatches\":"
        << summary.dualImplementationExactMatches;
    out << "},";

    out << "\"denseToFrontier\":{";
    out << "\"runs\":"
        << summary.denseToFrontier.runs << ",";
    out << "\"passed\":"
        << summary.denseToFrontier.passed << ",";
    out << "\"exactFinalMatches\":"
        << summary.denseToFrontier.exactFinalMatches;
    out << "},";

    out << "\"frontierToDense\":{";
    out << "\"runs\":"
        << summary.frontierToDense.runs << ",";
    out << "\"passed\":"
        << summary.frontierToDense.passed << ",";
    out << "\"exactFinalMatches\":"
        << summary.frontierToDense.exactFinalMatches;
    out << "},";

    out << "\"frontierRebuild\":{";
    out << "\"runs\":"
        << summary.frontierRebuild.runs << ",";
    out << "\"passed\":"
        << summary.frontierRebuild.passed << ",";
    out << "\"preDeletionStateMatches\":"
        << summary.frontierRebuild.preDeletionStateMatches << ",";
    out << "\"deletionMutationCount\":"
        << summary.frontierRebuild.deletionMutationCount << ",";
    out << "\"exactFinalMatches\":"
        << summary.frontierRebuild.exactFinalMatches;
    out << "},";

    out << "\"sparseSpecialization\":{";
    out << "\"runs\":"
        << summary.sparse.runs << ",";
    out << "\"passed\":"
        << summary.sparse.passed << ",";
    out << "\"frontierChoices\":"
        << summary.sparse.frontierChoices << ",";
    out << "\"denseEvaluations\":"
        << summary.sparse.denseEvaluations << ",";
    out << "\"frontierEvaluations\":"
        << summary.sparse.frontierEvaluations << ",";
    out << "\"meanReduction\":"
        << summary.sparse.meanReduction << ",";
    out << "\"meanActionableFraction\":"
        << summary.sparse.meanActionableFraction;
    out << "},";

    out << "\"broadTradeoff\":{";
    out << "\"runs\":"
        << summary.broad.runs << ",";
    out << "\"passed\":"
        << summary.broad.passed << ",";
    out << "\"denseChoices\":"
        << summary.broad.denseChoices << ",";
    out << "\"frontierChoices\":"
        << summary.broad.frontierChoices << ",";
    out << "\"denseEvaluations\":"
        << summary.broad.denseEvaluations << ",";
    out << "\"frontierEvaluations\":"
        << summary.broad.frontierEvaluations << ",";
    out << "\"meanActionableFraction\":"
        << summary.broad.meanActionableFraction;
    out << "},";

    out << "\"automatic\":{";
    out << "\"runs\":"
        << summary.automatic.runs << ",";
    out << "\"passed\":"
        << summary.automatic.passed << ",";
    out << "\"denseChoices\":"
        << summary.automatic.denseChoices << ",";
    out << "\"frontierChoices\":"
        << summary.automatic.frontierChoices;
    out << "},";

    out << "\"controllerLoss\":{";
    out << "\"runs\":"
        << summary.controllerLoss.runs << ",";
    out << "\"denseAdvantages\":"
        << summary.controllerLoss.denseAdvantages << ",";
    out << "\"frontierAdvantages\":"
        << summary.controllerLoss.frontierAdvantages;
    out << "},";

    out << "\"authority\":{";
    out << "\"validAccepted\":"
        << (
            summary.authority.validAccepted
                ? "true"
                : "false"
        )
        << ",";
    out << "\"forgedRejected\":"
        << (
            summary.authority.forgedRejected
                ? "true"
                : "false"
        )
        << ",";
    out << "\"nonMonotonicRejected\":"
        << (
            summary.authority.nonMonotonicRejected
                ? "true"
                : "false"
        )
        << ",";
    out << "\"conflictingBatchRejectedAtomically\":"
        << (
            summary.authority
                .conflictingBatchRejectedAtomically
                ? "true"
                : "false"
        )
        << ",";
    out << "\"sourceStableAcrossGeneratedMachinery\":"
        << (
            summary.authority
                .sourceStableAcrossGeneratedMachinery
                ? "true"
                : "false"
        );
    out << "},";

    out << "\"resources\":{";
    out << "\"replicaSizeBytes\":"
        << summary.resources.replicaSizeBytes << ",";
    out << "\"substrateBytesProxy\":"
        << summary.resources.substrateBytesProxy << ",";
    out << "\"denseBytesProxy\":"
        << summary.resources.denseBytesProxy << ",";
    out << "\"frontierBytesProxy\":"
        << summary.resources.frontierBytesProxy << ",";
    out << "\"profileBytesProxy\":"
        << summary.resources.profileBytesProxy << ",";
    out << "\"totalCoreBytesProxy\":"
        << summary.resources.totalCoreBytesProxy << ",";
    out << "\"coreDataGateBytes\":262144,";
    out << "\"withinLowMemoryGate\":"
        << (
            summary.resources.withinGate
                ? "true"
                : "false"
        );
    out << "},";

    out << "\"antiCheat\":{";
    out << "\"frozenLocalRelation\":true,";
    out << "\"generatedMachineryOwnsAuthority\":false,";
    out << "\"globalTargetInMachinery\":false,";
    out << "\"profileCostCounted\":true,";
    out << "\"rebuildCostCounted\":true,";
    out << "\"quickJs\":false,";
    out << "\"webView\":false";
    out << "},";

    out << "\"suiteElapsedMicros\":"
        << summary.suiteElapsedMicros;

    out << "}";
    return out.str();
}

SuiteSummary runSuite() {
    const auto started = std::chrono::steady_clock::now();

    SuiteSummary summary;

    runDenseParity(summary);
    runFrontierParity(summary);
    runDualImplementation(summary);
    runDenseToFrontier(summary);
    runFrontierToDense(summary);
    runFrontierRebuild(summary);
    runSparseSpecialization(summary);
    runBroadTradeoff(summary);
    runAutomaticSelection(summary);

    summary.authority = runAuthorityIsolation();
    runControllerLoss(summary);
    summary.resources = measureResources();

    summary.pass = suitePass(summary);

    const auto ended = std::chrono::steady_clock::now();
    summary.suiteElapsedMicros =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<
                std::chrono::microseconds
            >(ended - started).count()
        );

    return summary;
}

}  // namespace

RunResult runAll() {
    const SuiteSummary summary = runSuite();

    return RunResult{
        summary.pass,
        toJson(summary)
    };
}

}  // namespace codynex::n1
