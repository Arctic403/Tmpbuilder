#include "n2_primitives.h"

#include <algorithm>
#include <chrono>
#include <limits>

namespace codynex::n2 {
namespace {

const std::vector<PrimitiveDescriptor> kCatalogue = {
    {
        PrimitiveId::SweepOnce,
        "sweep-once",
        true,
        false,
        true,
        false
    },
    {
        PrimitiveId::SeedActionable,
        "seed-actionable",
        true,
        true,
        false,
        false
    },
    {
        PrimitiveId::DrainLocalWork,
        "drain-local-work",
        false,
        true,
        true,
        false
    }
};

std::uint64_t hashBytes(
    std::uint64_t hash,
    const char* text
) {
    if (text == nullptr) {
        return hash;
    }

    for (const char* p = text; *p != '\0'; ++p) {
        hash ^= static_cast<std::uint8_t>(*p);
        hash *= 1099511628211ULL;
    }

    return hash;
}

}  // namespace

const std::vector<PrimitiveDescriptor>& primitiveCatalogue() {
    return kCatalogue;
}

std::uint64_t primitiveCatalogueHash() {
    std::uint64_t hash = 1469598103934665603ULL;

    for (const PrimitiveDescriptor& descriptor : kCatalogue) {
        hash ^= static_cast<std::uint64_t>(descriptor.id);
        hash *= 1099511628211ULL;
        hash = hashBytes(hash, descriptor.name);
        hash ^= descriptor.scansSubstrate ? 1ULL : 0ULL;
        hash *= 1099511628211ULL;
        hash ^= descriptor.usesWorkQueue ? 1ULL : 0ULL;
        hash *= 1099511628211ULL;
        hash ^= descriptor.mutatesSubstrate ? 1ULL : 0ULL;
        hash *= 1099511628211ULL;
        hash ^= descriptor.carriesAuthority ? 1ULL : 0ULL;
        hash *= 1099511628211ULL;
    }

    return hash;
}

std::size_t primitiveCatalogueBytesProxy() {
    return kCatalogue.capacity() * sizeof(PrimitiveDescriptor);
}

PrimitiveMetrics combinePrimitiveMetrics(
    const PrimitiveMetrics& a,
    const PrimitiveMetrics& b
) {
    PrimitiveMetrics out;
    out.relationEvaluations =
        a.relationEvaluations + b.relationEvaluations;
    out.changes = a.changes + b.changes;
    out.queuePushes = a.queuePushes + b.queuePushes;
    out.queuePops = a.queuePops + b.queuePops;
    out.elapsedMicros = a.elapsedMicros + b.elapsedMicros;
    return out;
}

PrimitiveScratch::PrimitiveScratch(std::size_t replicaCount)
    : queue_(replicaCount),
      queued_(replicaCount, 0U),
      order_(replicaCount) {}

void PrimitiveScratch::clearQueue() {
    std::fill(queued_.begin(), queued_.end(), 0U);
    head_ = 0U;
    tail_ = 0U;
    count_ = 0U;
}

void PrimitiveScratch::prepareOrder(
    Schedule schedule,
    Rng& rng
) {
    const std::size_t n = order_.size();

    for (std::size_t i = 0U; i < n; ++i) {
        order_[i] = i;
    }

    if (schedule == Schedule::Reverse) {
        std::reverse(order_.begin(), order_.end());
        return;
    }

    if (schedule != Schedule::Random) {
        return;
    }

    for (std::size_t k = n; k > 1U; --k) {
        const std::size_t j = rng.index(k);
        std::swap(order_[k - 1U], order_[j]);
    }
}

bool PrimitiveScratch::enqueue(std::size_t index) {
    if (
        queue_.empty() ||
        index >= queued_.size() ||
        queued_[index] != 0U ||
        count_ >= queue_.size()
    ) {
        return false;
    }

    queue_[tail_] = index;
    tail_ = (tail_ + 1U) % queue_.size();
    queued_[index] = 1U;
    ++count_;
    return true;
}

bool PrimitiveScratch::empty() const {
    return count_ == 0U;
}

std::size_t PrimitiveScratch::pop() {
    if (count_ == 0U || queue_.empty()) {
        return std::numeric_limits<std::size_t>::max();
    }

    const std::size_t index = queue_[head_];
    head_ = (head_ + 1U) % queue_.size();
    --count_;

    if (index < queued_.size()) {
        queued_[index] = 0U;
    }

    return index;
}

std::size_t PrimitiveScratch::queuedCount() const {
    return count_;
}

const std::vector<std::size_t>& PrimitiveScratch::order() const {
    return order_;
}

std::size_t PrimitiveScratch::bytesProxy() const {
    return
        queue_.capacity() * sizeof(std::size_t) +
        queued_.capacity() * sizeof(std::uint8_t) +
        order_.capacity() * sizeof(std::size_t);
}

PrimitiveExecutor::PrimitiveExecutor(std::size_t replicaCount)
    : scratch_(replicaCount) {}

PrimitiveStepResult PrimitiveExecutor::execute(
    PrimitiveId id,
    Mesh& mesh,
    Schedule schedule,
    Rng& rng,
    std::uint64_t budget
) {
    switch (id) {
        case PrimitiveId::SweepOnce:
            return sweepOnce(mesh, schedule, rng);
        case PrimitiveId::SeedActionable:
            return seedActionable(mesh, schedule, rng);
        case PrimitiveId::DrainLocalWork:
            return drainLocalWork(mesh, budget);
    }

    PrimitiveStepResult result;
    result.guaranteeSatisfied =
        codynex::n2::guaranteeSatisfied(measure(mesh));
    result.queueEmpty = scratch_.empty();
    return result;
}

void PrimitiveExecutor::destroyGeneratedScratch() {
    scratch_.clearQueue();
}

std::size_t PrimitiveExecutor::scratchBytesProxy() const {
    return scratch_.bytesProxy();
}

PrimitiveStepResult PrimitiveExecutor::sweepOnce(
    Mesh& mesh,
    Schedule schedule,
    Rng& rng
) {
    const auto started = std::chrono::steady_clock::now();

    PrimitiveStepResult result;
    scratch_.prepareOrder(schedule, rng);

    for (const std::size_t index : scratch_.order()) {
        if (mesh.cell(index).pinned) {
            continue;
        }

        const Replica evaluated = mesh.evaluateLocal(index);
        ++result.metrics.relationEvaluations;

        if (mesh.applyEvaluated(index, evaluated)) {
            ++result.metrics.changes;
        }
    }

    const auto ended = std::chrono::steady_clock::now();
    result.metrics.elapsedMicros =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<
                std::chrono::microseconds
            >(ended - started).count()
        );

    result.guaranteeSatisfied =
        codynex::n2::guaranteeSatisfied(measure(mesh));
    result.queueEmpty = scratch_.empty();
    return result;
}

PrimitiveStepResult PrimitiveExecutor::seedActionable(
    const Mesh& mesh,
    Schedule schedule,
    Rng& rng
) {
    const auto started = std::chrono::steady_clock::now();

    PrimitiveStepResult result;
    scratch_.clearQueue();
    scratch_.prepareOrder(schedule, rng);

    for (const std::size_t index : scratch_.order()) {
        if (mesh.cell(index).pinned) {
            continue;
        }

        ++result.metrics.relationEvaluations;

        if (
            mesh.isActionable(index) &&
            scratch_.enqueue(index)
        ) {
            ++result.metrics.queuePushes;
        }
    }

    const auto ended = std::chrono::steady_clock::now();
    result.metrics.elapsedMicros =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<
                std::chrono::microseconds
            >(ended - started).count()
        );

    result.guaranteeSatisfied =
        codynex::n2::guaranteeSatisfied(measure(mesh));
    result.queueEmpty = scratch_.empty();
    return result;
}

PrimitiveStepResult PrimitiveExecutor::drainLocalWork(
    Mesh& mesh,
    std::uint64_t budget
) {
    const auto started = std::chrono::steady_clock::now();

    PrimitiveStepResult result;

    while (
        !scratch_.empty() &&
        result.metrics.queuePops < budget
    ) {
        const std::size_t index = scratch_.pop();

        if (
            index ==
                std::numeric_limits<std::size_t>::max() ||
            index >= mesh.size()
        ) {
            break;
        }

        ++result.metrics.queuePops;

        if (mesh.cell(index).pinned) {
            continue;
        }

        const Replica evaluated = mesh.evaluateLocal(index);
        ++result.metrics.relationEvaluations;

        if (!mesh.applyEvaluated(index, evaluated)) {
            continue;
        }

        ++result.metrics.changes;

        std::array<std::size_t, 4> neighbors{};
        const std::size_t neighborCount =
            mesh.neighborIndices(index, neighbors);

        for (
            std::size_t i = 0U;
            i < neighborCount;
            ++i
        ) {
            const std::size_t neighbor = neighbors[i];

            if (
                !mesh.cell(neighbor).pinned &&
                scratch_.enqueue(neighbor)
            ) {
                ++result.metrics.queuePushes;
            }
        }
    }

    const auto ended = std::chrono::steady_clock::now();
    result.metrics.elapsedMicros =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<
                std::chrono::microseconds
            >(ended - started).count()
        );

    result.guaranteeSatisfied =
        codynex::n2::guaranteeSatisfied(measure(mesh));
    result.queueEmpty = scratch_.empty();
    return result;
}

}  // namespace codynex::n2
