#pragma once

#include "n1_core.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace codynex::n1 {

enum class MachineryKind {
    Dense,
    Frontier
};

const char* machineryName(MachineryKind kind);

struct WorkStats {
    std::uint64_t relationEvaluations = 0U;
    std::uint64_t generationEvaluations = 0U;
    std::uint64_t changes = 0U;
    std::uint64_t queuePushes = 0U;
    std::uint64_t queuePops = 0U;
    std::uint64_t sweeps = 0U;
    std::uint64_t elapsedMicros = 0U;
    std::size_t machineryBytesProxy = 0U;
};

WorkStats combineWork(
    const WorkStats& first,
    const WorkStats& second
);

struct SpecializationProfile {
    std::size_t cellCount = 0U;
    std::size_t ordinaryCount = 0U;
    std::size_t actionableCount = 0U;
    double actionableFraction = 0.0;
    std::uint64_t relationEvaluations = 0U;
    std::uint64_t elapsedMicros = 0U;
    std::size_t bytesProxy = 0U;
};

SpecializationProfile profileMesh(const Mesh& mesh);

MachineryKind chooseMachinery(
    const SpecializationProfile& profile,
    double frontierThreshold
);

class DenseMachinery {
public:
    explicit DenseMachinery(std::size_t replicaCount);

    WorkStats runUntil(
        Mesh& mesh,
        Schedule schedule,
        std::uint32_t schedulerSeed,
        int maxSweeps
    );

    WorkStats runSweeps(
        Mesh& mesh,
        Schedule schedule,
        Rng& rng,
        int sweepCount
    );

    std::size_t bytesProxy() const;

private:
    void prepareOrder(
        Schedule schedule,
        Rng& rng
    );

    std::vector<std::size_t> order_;
};

class FrontierMachinery {
public:
    explicit FrontierMachinery(std::size_t replicaCount);

    WorkStats rebuild(
        const Mesh& mesh,
        Schedule schedule,
        Rng& rng
    );

    WorkStats processBudget(
        Mesh& mesh,
        std::uint64_t maxPops
    );

    WorkStats runToEmpty(
        Mesh& mesh,
        std::uint64_t maxPops
    );

    bool empty() const;
    std::size_t queuedCount() const;
    std::size_t bytesProxy() const;

private:
    void clear();
    void prepareScanOrder(
        Schedule schedule,
        Rng& rng
    );
    bool enqueue(std::size_t index);
    std::size_t pop();

    std::vector<std::size_t> queue_;
    std::vector<std::uint8_t> queued_;
    std::vector<std::size_t> scanOrder_;
    std::size_t head_ = 0U;
    std::size_t tail_ = 0U;
    std::size_t count_ = 0U;
};

}  // namespace codynex::n1
