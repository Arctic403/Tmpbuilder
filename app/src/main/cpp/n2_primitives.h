#pragma once

#include "n2_core.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace codynex::n2 {

enum class PrimitiveId : std::uint8_t {
    SweepOnce = 1U,
    SeedActionable = 2U,
    DrainLocalWork = 3U
};

struct PrimitiveDescriptor {
    PrimitiveId id;
    const char* name;
    bool scansSubstrate;
    bool usesWorkQueue;
    bool mutatesSubstrate;
    bool carriesAuthority;
};

const std::vector<PrimitiveDescriptor>& primitiveCatalogue();
std::uint64_t primitiveCatalogueHash();
std::size_t primitiveCatalogueBytesProxy();

struct PrimitiveMetrics {
    std::uint64_t relationEvaluations = 0U;
    std::uint64_t changes = 0U;
    std::uint64_t queuePushes = 0U;
    std::uint64_t queuePops = 0U;
    std::uint64_t elapsedMicros = 0U;
};

PrimitiveMetrics combinePrimitiveMetrics(
    const PrimitiveMetrics& a,
    const PrimitiveMetrics& b
);

class PrimitiveScratch {
public:
    explicit PrimitiveScratch(std::size_t replicaCount);

    void clearQueue();
    void prepareOrder(Schedule schedule, Rng& rng);

    bool enqueue(std::size_t index);
    bool empty() const;
    std::size_t pop();
    std::size_t queuedCount() const;

    const std::vector<std::size_t>& order() const;
    std::size_t bytesProxy() const;

private:
    std::vector<std::size_t> queue_;
    std::vector<std::uint8_t> queued_;
    std::vector<std::size_t> order_;
    std::size_t head_ = 0U;
    std::size_t tail_ = 0U;
    std::size_t count_ = 0U;
};

struct PrimitiveStepResult {
    PrimitiveMetrics metrics;
    bool guaranteeSatisfied = false;
    bool queueEmpty = true;
};

class PrimitiveExecutor {
public:
    explicit PrimitiveExecutor(std::size_t replicaCount);

    PrimitiveStepResult execute(
        PrimitiveId id,
        Mesh& mesh,
        Schedule schedule,
        Rng& rng,
        std::uint64_t budget
    );

    void destroyGeneratedScratch();
    std::size_t scratchBytesProxy() const;

private:
    PrimitiveStepResult sweepOnce(
        Mesh& mesh,
        Schedule schedule,
        Rng& rng
    );

    PrimitiveStepResult seedActionable(
        const Mesh& mesh,
        Schedule schedule,
        Rng& rng
    );

    PrimitiveStepResult drainLocalWork(
        Mesh& mesh,
        std::uint64_t budget
    );

    PrimitiveScratch scratch_;
};

}  // namespace codynex::n2
