#include "n1_machinery.h"

#include <algorithm>
#include <chrono>
#include <limits>

namespace codynex::n1 {

const char* machineryName(MachineryKind kind) {
    switch (kind) {
        case MachineryKind::Dense:
            return "dense";
        case MachineryKind::Frontier:
            return "frontier";
    }

    return "unknown";
}

WorkStats combineWork(
    const WorkStats& first,
    const WorkStats& second
) {
    WorkStats out;
    out.relationEvaluations =
        first.relationEvaluations +
        second.relationEvaluations;
    out.generationEvaluations =
        first.generationEvaluations +
        second.generationEvaluations;
    out.changes =
        first.changes +
        second.changes;
    out.queuePushes =
        first.queuePushes +
        second.queuePushes;
    out.queuePops =
        first.queuePops +
        second.queuePops;
    out.sweeps =
        first.sweeps +
        second.sweeps;
    out.elapsedMicros =
        first.elapsedMicros +
        second.elapsedMicros;
    out.machineryBytesProxy = std::max(
        first.machineryBytesProxy,
        second.machineryBytesProxy
    );
    return out;
}

SpecializationProfile profileMesh(const Mesh& mesh) {
    const auto started = std::chrono::steady_clock::now();

    SpecializationProfile profile;
    profile.cellCount = mesh.size();

    for (std::size_t i = 0U; i < mesh.size(); ++i) {
        if (mesh.cell(i).pinned) {
            continue;
        }

        ++profile.ordinaryCount;
        ++profile.relationEvaluations;

        if (mesh.isActionable(i)) {
            ++profile.actionableCount;
        }
    }

    profile.actionableFraction =
        profile.ordinaryCount > 0U
            ? static_cast<double>(profile.actionableCount) /
                static_cast<double>(profile.ordinaryCount)
            : 0.0;

    profile.bytesProxy = sizeof(SpecializationProfile);

    const auto ended = std::chrono::steady_clock::now();
    profile.elapsedMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            ended - started
        ).count()
    );

    return profile;
}

MachineryKind chooseMachinery(
    const SpecializationProfile& profile,
    double frontierThreshold
) {
    return
        profile.actionableFraction <= frontierThreshold
            ? MachineryKind::Frontier
            : MachineryKind::Dense;
}

DenseMachinery::DenseMachinery(std::size_t replicaCount)
    : order_(replicaCount) {}

void DenseMachinery::prepareOrder(
    Schedule schedule,
    Rng& rng
) {
    const std::size_t n = order_.size();

    if (schedule == Schedule::Forward) {
        for (std::size_t i = 0U; i < n; ++i) {
            order_[i] = i;
        }
        return;
    }

    if (schedule == Schedule::Reverse) {
        for (std::size_t i = 0U; i < n; ++i) {
            order_[i] = n - 1U - i;
        }
        return;
    }

    for (std::size_t i = 0U; i < n; ++i) {
        order_[i] = i;
    }

    for (std::size_t k = n; k > 1U; --k) {
        const std::size_t j = rng.index(k);
        std::swap(order_[k - 1U], order_[j]);
    }
}

WorkStats DenseMachinery::runUntil(
    Mesh& mesh,
    Schedule schedule,
    std::uint32_t schedulerSeed,
    int maxSweeps
) {
    const auto started = std::chrono::steady_clock::now();

    WorkStats stats;
    Rng rng(schedulerSeed);

    for (int sweep = 0; sweep < maxSweeps; ++sweep) {
        if (guaranteeSatisfied(measure(mesh))) {
            break;
        }

        prepareOrder(schedule, rng);
        std::uint64_t changedThisSweep = 0U;

        for (const std::size_t index : order_) {
            if (mesh.cell(index).pinned) {
                continue;
            }

            const Replica evaluated = mesh.evaluateLocal(index);
            ++stats.relationEvaluations;

            if (mesh.applyEvaluated(index, evaluated)) {
                ++stats.changes;
                ++changedThisSweep;
            }
        }

        ++stats.sweeps;

        if (
            changedThisSweep == 0U &&
            !guaranteeSatisfied(measure(mesh))
        ) {
            break;
        }
    }

    const auto ended = std::chrono::steady_clock::now();
    stats.elapsedMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            ended - started
        ).count()
    );
    stats.machineryBytesProxy = bytesProxy();

    return stats;
}

WorkStats DenseMachinery::runSweeps(
    Mesh& mesh,
    Schedule schedule,
    Rng& rng,
    int sweepCount
) {
    const auto started = std::chrono::steady_clock::now();

    WorkStats stats;

    for (int sweep = 0; sweep < sweepCount; ++sweep) {
        if (guaranteeSatisfied(measure(mesh))) {
            break;
        }

        prepareOrder(schedule, rng);

        for (const std::size_t index : order_) {
            if (mesh.cell(index).pinned) {
                continue;
            }

            const Replica evaluated = mesh.evaluateLocal(index);
            ++stats.relationEvaluations;

            if (mesh.applyEvaluated(index, evaluated)) {
                ++stats.changes;
            }
        }

        ++stats.sweeps;
    }

    const auto ended = std::chrono::steady_clock::now();
    stats.elapsedMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            ended - started
        ).count()
    );
    stats.machineryBytesProxy = bytesProxy();

    return stats;
}

std::size_t DenseMachinery::bytesProxy() const {
    return order_.capacity() * sizeof(std::size_t);
}

FrontierMachinery::FrontierMachinery(std::size_t replicaCount)
    : queue_(replicaCount),
      queued_(replicaCount, 0U),
      scanOrder_(replicaCount) {}

void FrontierMachinery::clear() {
    std::fill(queued_.begin(), queued_.end(), 0U);
    head_ = 0U;
    tail_ = 0U;
    count_ = 0U;
}

void FrontierMachinery::prepareScanOrder(
    Schedule schedule,
    Rng& rng
) {
    const std::size_t n = scanOrder_.size();

    if (schedule == Schedule::Forward) {
        for (std::size_t i = 0U; i < n; ++i) {
            scanOrder_[i] = i;
        }
        return;
    }

    if (schedule == Schedule::Reverse) {
        for (std::size_t i = 0U; i < n; ++i) {
            scanOrder_[i] = n - 1U - i;
        }
        return;
    }

    for (std::size_t i = 0U; i < n; ++i) {
        scanOrder_[i] = i;
    }

    for (std::size_t k = n; k > 1U; --k) {
        const std::size_t j = rng.index(k);
        std::swap(scanOrder_[k - 1U], scanOrder_[j]);
    }
}

bool FrontierMachinery::enqueue(std::size_t index) {
    if (
        index >= queued_.size() ||
        queued_[index] != 0U ||
        queue_.empty() ||
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

std::size_t FrontierMachinery::pop() {
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

WorkStats FrontierMachinery::rebuild(
    const Mesh& mesh,
    Schedule schedule,
    Rng& rng
) {
    const auto started = std::chrono::steady_clock::now();

    clear();
    prepareScanOrder(schedule, rng);

    WorkStats stats;

    for (const std::size_t index : scanOrder_) {
        if (mesh.cell(index).pinned) {
            continue;
        }

        ++stats.relationEvaluations;
        ++stats.generationEvaluations;

        if (mesh.isActionable(index) && enqueue(index)) {
            ++stats.queuePushes;
        }
    }

    const auto ended = std::chrono::steady_clock::now();
    stats.elapsedMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            ended - started
        ).count()
    );
    stats.machineryBytesProxy = bytesProxy();

    return stats;
}

WorkStats FrontierMachinery::processBudget(
    Mesh& mesh,
    std::uint64_t maxPops
) {
    const auto started = std::chrono::steady_clock::now();

    WorkStats stats;

    while (
        count_ > 0U &&
        stats.queuePops < maxPops
    ) {
        const std::size_t index = pop();

        if (
            index == std::numeric_limits<std::size_t>::max() ||
            index >= mesh.size()
        ) {
            break;
        }

        ++stats.queuePops;

        if (mesh.cell(index).pinned) {
            continue;
        }

        const Replica evaluated = mesh.evaluateLocal(index);
        ++stats.relationEvaluations;

        if (!mesh.applyEvaluated(index, evaluated)) {
            continue;
        }

        ++stats.changes;

        std::array<std::size_t, 4> neighbors{};
        const std::size_t neighborCount =
            mesh.neighborIndices(index, neighbors);

        for (std::size_t i = 0U; i < neighborCount; ++i) {
            const std::size_t neighbor = neighbors[i];

            if (
                !mesh.cell(neighbor).pinned &&
                enqueue(neighbor)
            ) {
                ++stats.queuePushes;
            }
        }
    }

    const auto ended = std::chrono::steady_clock::now();
    stats.elapsedMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            ended - started
        ).count()
    );
    stats.machineryBytesProxy = bytesProxy();

    return stats;
}

WorkStats FrontierMachinery::runToEmpty(
    Mesh& mesh,
    std::uint64_t maxPops
) {
    return processBudget(mesh, maxPops);
}

bool FrontierMachinery::empty() const {
    return count_ == 0U;
}

std::size_t FrontierMachinery::queuedCount() const {
    return count_;
}

std::size_t FrontierMachinery::bytesProxy() const {
    return
        queue_.capacity() * sizeof(std::size_t) +
        queued_.capacity() * sizeof(std::uint8_t) +
        scanOrder_.capacity() * sizeof(std::size_t);
}

}  // namespace codynex::n1
