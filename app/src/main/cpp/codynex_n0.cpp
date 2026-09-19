#include "codynex_n0.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace codynex::n0 {
namespace {

constexpr std::int32_t kRootRank = 32767;
constexpr int kWidth = 16;
constexpr int kHeight = 16;
constexpr int kMaxSweeps = 128;
constexpr double kAgreementGate = 0.99;
constexpr double kInformedGate = 0.99;
constexpr std::array<std::uint32_t, 6> kSeeds = {
    101U, 1009U, 4093U, 8191U, 16381U, 32771U
};

enum class Schedule {
    Forward,
    Reverse,
    Random
};

const char* scheduleName(Schedule schedule) {
    switch (schedule) {
        case Schedule::Forward:
            return "forward";
        case Schedule::Reverse:
            return "reverse";
        case Schedule::Random:
            return "random";
    }
    return "unknown";
}

class Rng {
public:
    explicit Rng(std::uint32_t seed)
        : state_(seed == 0U ? 0x9e3779b9U : seed) {}

    std::uint32_t nextU32() {
        state_ ^= state_ << 13U;
        state_ ^= state_ >> 17U;
        state_ ^= state_ << 5U;
        return state_;
    }

    std::size_t index(std::size_t upperExclusive) {
        if (upperExclusive == 0U) {
            return 0U;
        }
        return static_cast<std::size_t>(
            nextU32() % static_cast<std::uint32_t>(upperExclusive)
        );
    }

    std::uint8_t byte() {
        return static_cast<std::uint8_t>(nextU32() & 0xffU);
    }

private:
    std::uint32_t state_;
};

struct Replica {
    std::int32_t version = 0;
    std::uint8_t data = 0U;
    std::int32_t rank = 0;
    bool pinned = false;
    bool conflict = false;
};

struct LocalView {
    Replica self;
    std::array<Replica, 4> neighbors{};
    std::size_t neighborCount = 0U;
};

struct CandidateSupport {
    bool found = false;
    std::int32_t version = -1;
    std::uint8_t data = 0U;
    std::int32_t rank = -1;
    bool conflict = false;
};

CandidateSupport candidateSupport(const LocalView& view) {
    CandidateSupport support;

    for (std::size_t i = 0; i < view.neighborCount; ++i) {
        const Replica& neighbor = view.neighbors[i];

        if (neighbor.rank <= 0 || neighbor.conflict) {
            continue;
        }

        const bool relevant =
            view.self.rank <= 0 ||
            neighbor.version > view.self.version ||
            (
                neighbor.version == view.self.version &&
                neighbor.rank > view.self.rank
            );

        if (!relevant) {
            continue;
        }

        if (
            neighbor.version > support.version ||
            (
                neighbor.version == support.version &&
                neighbor.rank > support.rank
            )
        ) {
            support.found = true;
            support.version = neighbor.version;
            support.data = neighbor.data;
            support.rank = neighbor.rank;
            support.conflict = false;
        } else if (
            neighbor.version == support.version &&
            neighbor.rank == support.rank &&
            neighbor.data != support.data
        ) {
            support.conflict = true;
        }
    }

    return support;
}

// N0 local workload semantics.
// Deliberately accepts only a bounded LocalView: no mesh, dimensions,
// coordinates, global target, model/factorization, controller, JNI or Android.
Replica chooseReplica(const LocalView& view) {
    if (view.self.pinned) {
        Replica out = view.self;
        out.conflict = false;
        return out;
    }

    const CandidateSupport support = candidateSupport(view);

    if (!support.found) {
        Replica out = view.self;
        out.rank = 0;
        return out;
    }

    if (support.conflict) {
        Replica out = view.self;
        out.version = std::max(view.self.version, support.version);
        out.rank = 0;
        out.conflict = true;
        return out;
    }

    Replica out;
    out.version = support.version;
    out.data = support.data;
    out.rank = std::max<std::int32_t>(0, support.rank - 1);
    out.pinned = false;
    out.conflict = false;
    return out;
}

class Mesh {
public:
    Mesh(int width, int height, std::uint32_t seed)
        : width_(width),
          height_(height),
          seed_(seed),
          cells_(
              static_cast<std::size_t>(width) *
              static_cast<std::size_t>(height)
          ) {
        Rng rng(seed_);
        for (Replica& cell : cells_) {
            cell.version = 0;
            cell.data = rng.byte();
            cell.rank = 0;
            cell.pinned = false;
            cell.conflict = false;
        }

        if (!cells_.empty()) {
            cells_[0] = Replica{0, 0U, kRootRank, true, false};
        }
    }

    int width() const {
        return width_;
    }

    int height() const {
        return height_;
    }

    std::size_t size() const {
        return cells_.size();
    }

    const Replica& cell(std::size_t index) const {
        return cells_.at(index);
    }

    Replica& cell(std::size_t index) {
        return cells_.at(index);
    }

    const Replica& source() const {
        return cells_.front();
    }

    void setSource(std::int32_t version, std::uint8_t data) {
        cells_.front() = Replica{
            version,
            data,
            kRootRank,
            true,
            false
        };
    }

    std::size_t neighborIndices(
        std::size_t index,
        std::array<std::size_t, 4>& out
    ) const {
        const int x = static_cast<int>(
            index % static_cast<std::size_t>(width_)
        );
        const int y = static_cast<int>(
            index / static_cast<std::size_t>(width_)
        );

        std::size_t count = 0U;
        if (y > 0) {
            out[count++] = index - static_cast<std::size_t>(width_);
        }
        if (x + 1 < width_) {
            out[count++] = index + 1U;
        }
        if (y + 1 < height_) {
            out[count++] = index + static_cast<std::size_t>(width_);
        }
        if (x > 0) {
            out[count++] = index - 1U;
        }
        return count;
    }

    LocalView localView(std::size_t index) const {
        LocalView view;
        view.self = cells_.at(index);

        std::array<std::size_t, 4> neighbors{};
        const std::size_t count = neighborIndices(index, neighbors);
        view.neighborCount = count;

        for (std::size_t i = 0; i < count; ++i) {
            view.neighbors[i] = cells_.at(neighbors[i]);
        }

        return view;
    }

    bool applyLocal(std::size_t index) {
        Replica& current = cells_.at(index);
        if (current.pinned) {
            return false;
        }

        Replica next = chooseReplica(localView(index));
        next.pinned = false;
        next.rank = std::max<std::int32_t>(
            0,
            std::min<std::int32_t>(kRootRank - 1, next.rank)
        );

        if (
            current.version == next.version &&
            current.data == next.data &&
            current.rank == next.rank &&
            current.conflict == next.conflict
        ) {
            return false;
        }

        current = next;
        return true;
    }

    std::size_t resetFraction(double fraction, std::uint32_t seed) {
        Rng rng(seed);
        std::vector<std::size_t> indices;
        indices.reserve(cells_.size() > 0U ? cells_.size() - 1U : 0U);

        for (std::size_t i = 1U; i < cells_.size(); ++i) {
            indices.push_back(i);
        }

        for (std::size_t k = indices.size(); k > 1U; --k) {
            const std::size_t j = rng.index(k);
            std::swap(indices[k - 1U], indices[j]);
        }

        std::size_t count = indices.size();
        if (fraction < 1.0) {
            count = static_cast<std::size_t>(
                static_cast<double>(indices.size()) * fraction
            );
            count = std::max<std::size_t>(1U, count);
        }

        for (std::size_t i = 0U; i < count; ++i) {
            Replica& replica = cells_.at(indices[i]);
            replica.version = 0;
            replica.data = rng.byte();
            replica.rank = 0;
            replica.pinned = false;
            replica.conflict = false;
        }

        return count;
    }

    bool sameState(const Mesh& other) const {
        if (
            width_ != other.width_ ||
            height_ != other.height_ ||
            cells_.size() != other.cells_.size()
        ) {
            return false;
        }

        for (std::size_t i = 0U; i < cells_.size(); ++i) {
            const Replica& a = cells_[i];
            const Replica& b = other.cells_[i];

            if (
                a.version != b.version ||
                a.data != b.data ||
                a.rank != b.rank ||
                a.pinned != b.pinned ||
                a.conflict != b.conflict
            ) {
                return false;
            }
        }

        return true;
    }

    std::uint64_t signature() const {
        std::uint64_t hash = 1469598103934665603ULL;

        const auto add = [&hash](std::uint64_t value) {
            hash ^= value;
            hash *= 1099511628211ULL;
        };

        add(static_cast<std::uint64_t>(width_));
        add(static_cast<std::uint64_t>(height_));

        for (const Replica& replica : cells_) {
            add(static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(replica.version)
            ));
            add(static_cast<std::uint64_t>(replica.data));
            add(static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(replica.rank)
            ));
            add(replica.pinned ? 1ULL : 0ULL);
            add(replica.conflict ? 1ULL : 0ULL);
        }

        return hash;
    }

    std::size_t dataBytesProxy() const {
        return cells_.capacity() * sizeof(Replica);
    }

private:
    int width_;
    int height_;
    std::uint32_t seed_;
    std::vector<Replica> cells_;
};

struct SweepResult {
    std::uint64_t attempts = 0U;
    std::uint64_t changes = 0U;
};

class Scheduler {
public:
    explicit Scheduler(std::size_t replicaCount)
        : order_(replicaCount) {}

    SweepResult sweep(
        Mesh& mesh,
        Schedule schedule,
        Rng& rng
    ) {
        prepare(schedule, rng);

        SweepResult result;
        for (const std::size_t index : order_) {
            if (mesh.cell(index).pinned) {
                continue;
            }

            ++result.attempts;
            if (mesh.applyLocal(index)) {
                ++result.changes;
            }
        }

        return result;
    }

    std::size_t bytesProxy() const {
        return order_.capacity() * sizeof(std::size_t);
    }

private:
    void prepare(Schedule schedule, Rng& rng) {
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

    std::vector<std::size_t> order_;
};

struct Metric {
    double agreement = 0.0;
    double informed = 0.0;
    int conflicts = 0;
    int sourceVersion = 0;
    std::uint8_t sourceData = 0U;
};

Metric measure(const Mesh& mesh) {
    Metric metric;

    const Replica& source = mesh.source();
    metric.sourceVersion = source.version;
    metric.sourceData = source.data;

    std::size_t agreementCount = 0U;
    std::size_t informedCount = 0U;
    int conflicts = 0;

    for (std::size_t i = 0U; i < mesh.size(); ++i) {
        const Replica& replica = mesh.cell(i);

        if (
            replica.version == source.version &&
            replica.data == source.data &&
            !replica.conflict
        ) {
            ++agreementCount;
        }

        if (replica.rank > 0 && !replica.conflict) {
            ++informedCount;
        }

        if (replica.conflict) {
            ++conflicts;
        }
    }

    const double count = static_cast<double>(mesh.size());
    metric.agreement =
        count > 0.0
            ? static_cast<double>(agreementCount) / count
            : 1.0;
    metric.informed =
        count > 0.0
            ? static_cast<double>(informedCount) / count
            : 1.0;
    metric.conflicts = conflicts;

    return metric;
}

struct EvolutionResult {
    bool converged = false;
    int sweeps = 0;
    std::uint64_t localAttempts = 0U;
    std::uint64_t localChanges = 0U;
    std::uint64_t elapsedMicros = 0U;
    Metric metric;
    std::uint64_t signature = 0U;
};

EvolutionResult evolve(
    Mesh& mesh,
    Schedule schedule,
    std::uint32_t schedulerSeed,
    int maxSweeps
) {
    const auto started = std::chrono::steady_clock::now();

    Rng rng(schedulerSeed);
    Scheduler scheduler(mesh.size());

    EvolutionResult result;
    result.metric = measure(mesh);

    while (result.sweeps < maxSweeps) {
        if (
            result.metric.agreement >= kAgreementGate &&
            result.metric.informed >= kInformedGate &&
            result.metric.conflicts == 0
        ) {
            break;
        }

        const SweepResult sweep = scheduler.sweep(mesh, schedule, rng);
        ++result.sweeps;
        result.localAttempts += sweep.attempts;
        result.localChanges += sweep.changes;
        result.metric = measure(mesh);

        if (
            sweep.changes == 0U &&
            !(
                result.metric.agreement >= kAgreementGate &&
                result.metric.informed >= kInformedGate &&
                result.metric.conflicts == 0
            )
        ) {
            break;
        }
    }

    result.converged =
        result.metric.agreement >= kAgreementGate &&
        result.metric.informed >= kInformedGate &&
        result.metric.conflicts == 0;
    result.signature = mesh.signature();

    const auto ended = std::chrono::steady_clock::now();
    result.elapsedMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            ended - started
        ).count()
    );

    return result;
}

std::uint64_t mix64(std::uint64_t value) {
    value ^= value >> 30U;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27U;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31U;
    return value;
}

class SourceCapability {
public:
    SourceCapability() = default;

private:
    std::uint64_t id_ = 0U;
    std::uint64_t proof_ = 0U;

    SourceCapability(std::uint64_t id, std::uint64_t proof)
        : id_(id), proof_(proof) {}

    friend class ProtectedSourceBoundary;
};

struct PublishRequest {
    std::int32_t version = 0;
    std::uint8_t data = 0U;
};

struct PublishResult {
    bool ok = false;
    bool conflict = false;
    const char* reason = "not-authorized";
};

class ProtectedSourceBoundary {
public:
    explicit ProtectedSourceBoundary(std::uint64_t seed)
        : secret_(mix64(seed ^ 0xd1b54a32d192ed03ULL)) {}

    SourceCapability issue() {
        const std::uint64_t id = nextId_++;
        return SourceCapability(id, proofFor(id));
    }

    bool authorize(const SourceCapability& token) const {
        return
            token.id_ != 0U &&
            token.proof_ == proofFor(token.id_);
    }

    PublishResult publish(
        Mesh& mesh,
        const SourceCapability& token,
        const PublishRequest& request
    ) const {
        const PublishResult validation = validate(mesh, token, request);
        if (!validation.ok) {
            return validation;
        }

        mesh.setSource(request.version, request.data);
        return PublishResult{true, false, "ok"};
    }

    PublishResult publishPairAtomic(
        Mesh& mesh,
        const SourceCapability& firstToken,
        const PublishRequest& first,
        const SourceCapability& secondToken,
        const PublishRequest& second
    ) const {
        const PublishResult firstValidation =
            validate(mesh, firstToken, first);
        if (!firstValidation.ok) {
            return firstValidation;
        }

        const PublishResult secondValidation =
            validate(mesh, secondToken, second);
        if (!secondValidation.ok) {
            return secondValidation;
        }

        if (
            first.version != second.version ||
            first.data != second.data
        ) {
            return PublishResult{
                false,
                true,
                "incompatible-authorized-configs"
            };
        }

        mesh.setSource(first.version, first.data);
        return PublishResult{true, false, "ok"};
    }

private:
    PublishResult validate(
        const Mesh& mesh,
        const SourceCapability& token,
        const PublishRequest& request
    ) const {
        if (!authorize(token)) {
            return PublishResult{false, false, "not-authorized"};
        }

        if (request.version <= mesh.source().version) {
            return PublishResult{
                false,
                false,
                "non-monotonic-version"
            };
        }

        return PublishResult{true, false, "ok"};
    }

    std::uint64_t proofFor(std::uint64_t id) const {
        return mix64(secret_ ^ id ^ 0x9e3779b97f4a7c15ULL);
    }

    std::uint64_t secret_;
    std::uint64_t nextId_ = 1U;
};

struct BaselineController {
    std::int32_t version = 0;
    std::uint8_t data = 0U;
    bool available = true;
};

struct BaselineResult {
    bool available = false;
    std::uint64_t scans = 0U;
    std::uint64_t writes = 0U;
    std::uint64_t elapsedMicros = 0U;
};

bool directWrite(
    Replica& replica,
    std::int32_t version,
    std::uint8_t data
) {
    const bool changed =
        replica.version != version ||
        replica.data != data ||
        replica.rank <= 0 ||
        replica.conflict;

    replica.version = version;
    replica.data = data;
    replica.rank = 1;
    replica.pinned = false;
    replica.conflict = false;

    return changed;
}

BaselineResult baselineBroadcast(
    Mesh& mesh,
    const BaselineController& controller
) {
    const auto started = std::chrono::steady_clock::now();

    BaselineResult result;
    result.available = controller.available;

    if (controller.available) {
        result.scans = 1U;

        for (std::size_t i = 1U; i < mesh.size(); ++i) {
            if (
                directWrite(
                    mesh.cell(i),
                    controller.version,
                    controller.data
                )
            ) {
                ++result.writes;
            }
        }
    }

    const auto ended = std::chrono::steady_clock::now();
    result.elapsedMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            ended - started
        ).count()
    );

    return result;
}

struct Aggregate {
    int runs = 0;
    int passed = 0;
    std::uint64_t totalSweeps = 0U;
    std::uint64_t totalAttempts = 0U;
    std::uint64_t totalChanges = 0U;
    std::uint64_t totalElapsedMicros = 0U;
    int maxSweeps = 0;
    double minAgreement = 1.0;
    double minInformed = 1.0;
};

void addEvolution(
    Aggregate& aggregate,
    const EvolutionResult& result,
    bool passed
) {
    ++aggregate.runs;
    if (passed) {
        ++aggregate.passed;
    }

    aggregate.totalSweeps +=
        static_cast<std::uint64_t>(result.sweeps);
    aggregate.totalAttempts += result.localAttempts;
    aggregate.totalChanges += result.localChanges;
    aggregate.totalElapsedMicros += result.elapsedMicros;
    aggregate.maxSweeps = std::max(aggregate.maxSweeps, result.sweeps);
    aggregate.minAgreement =
        std::min(aggregate.minAgreement, result.metric.agreement);
    aggregate.minInformed =
        std::min(aggregate.minInformed, result.metric.informed);
}

struct BaselineAggregate {
    int healthyRuns = 0;
    std::uint64_t healthyScans = 0U;
    std::uint64_t healthyWrites = 0U;
    std::uint64_t healthyElapsedMicros = 0U;
    int unavailableRuns = 0;
    std::uint64_t unavailableScans = 0U;
    std::uint64_t unavailableWrites = 0U;
    std::uint64_t unavailableElapsedMicros = 0U;
};

struct AuthoritySummary {
    bool validAccepted = false;
    bool forgedRejected = false;
    bool nonMonotonicRejected = false;
    bool conflictingBatchRejectedAtomically = false;
};

struct SuiteSummary {
    bool pass = false;
    Aggregate initial;
    Aggregate updates;
    Aggregate repairs;
    Aggregate fullReset;
    Aggregate replay;
    int controllerLossRuns = 0;
    int controllerLossAdvantages = 0;
    BaselineAggregate baseline;
    AuthoritySummary authority;
    std::size_t replicaSize = 0U;
    std::size_t coreDataBytesProxy = 0U;
    std::uint64_t totalElapsedMicros = 0U;
};

std::uint8_t payloadFor(
    std::uint32_t seed,
    int scheduleIndex
) {
    return static_cast<std::uint8_t>(
        (
            seed * 37U +
            static_cast<std::uint32_t>(scheduleIndex * 17)
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

std::pair<Mesh, EvolutionResult> runInitial(
    std::uint32_t seed,
    Schedule schedule,
    std::uint8_t payload
) {
    Mesh mesh(kWidth, kHeight, seed);
    ProtectedSourceBoundary boundary(
        static_cast<std::uint64_t>(seed) ^ 0x1234abcdULL
    );
    const SourceCapability token = boundary.issue();

    const PublishResult publish = boundary.publish(
        mesh,
        token,
        PublishRequest{1, payload}
    );

    if (!publish.ok) {
        return {
            std::move(mesh),
            EvolutionResult{}
        };
    }

    EvolutionResult result = evolve(
        mesh,
        schedule,
        seed ^ scheduleSalt(schedule) ^ 0x0f0f0f0fU,
        kMaxSweeps
    );

    return {
        std::move(mesh),
        result
    };
}

AuthoritySummary runAuthorityChecks() {
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

    const std::uint64_t afterValid = mesh.signature();

    const PublishResult forgedResult = boundary.publish(
        mesh,
        forged,
        PublishRequest{2, 0x43U}
    );
    summary.forgedRejected =
        !forgedResult.ok &&
        mesh.signature() == afterValid;

    const PublishResult nonMonotonic = boundary.publish(
        mesh,
        valid,
        PublishRequest{1, 0x44U}
    );
    summary.nonMonotonicRejected =
        !nonMonotonic.ok &&
        mesh.signature() == afterValid;

    const PublishResult conflict = boundary.publishPairAtomic(
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

bool authorityPass(const AuthoritySummary& summary) {
    return
        summary.validAccepted &&
        summary.forgedRejected &&
        summary.nonMonotonicRejected &&
        summary.conflictingBatchRejectedAtomically;
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
    out << "\"minAgreement\":" << aggregate.minAgreement << ",";
    out << "\"minInformed\":" << aggregate.minInformed << ",";
    out << "\"maxSweeps\":" << aggregate.maxSweeps << ",";
    out << "\"totalSweeps\":" << aggregate.totalSweeps << ",";
    out << "\"localAttempts\":" << aggregate.totalAttempts << ",";
    out << "\"localChanges\":" << aggregate.totalChanges << ",";
    out << "\"elapsedMicros\":" << aggregate.totalElapsedMicros;
    out << "}";
}

std::string toJson(const SuiteSummary& summary) {
    std::ostringstream out;
    out << std::setprecision(12);

    out << "{";
    out << "\"pass\":" << (summary.pass ? "true" : "false") << ",";

    out << "\"config\":{";
    out << "\"width\":" << kWidth << ",";
    out << "\"height\":" << kHeight << ",";
    out << "\"seedCount\":" << kSeeds.size() << ",";
    out << "\"scheduleCount\":3,";
    out << "\"maxSweeps\":" << kMaxSweeps;
    out << "},";

    appendAggregateJson(out, "initial", summary.initial);
    out << ",";
    appendAggregateJson(out, "updates", summary.updates);
    out << ",";
    appendAggregateJson(out, "repairs", summary.repairs);
    out << ",";
    appendAggregateJson(out, "fullReset", summary.fullReset);
    out << ",";
    appendAggregateJson(out, "replay", summary.replay);
    out << ",";

    out << "\"controllerLoss\":{";
    out << "\"runs\":" << summary.controllerLossRuns << ",";
    out << "\"advantages\":"
        << summary.controllerLossAdvantages << ",";
    out << "\"passRate\":"
        << (
            summary.controllerLossRuns > 0
                ? static_cast<double>(summary.controllerLossAdvantages) /
                    static_cast<double>(summary.controllerLossRuns)
                : 0.0
        );
    out << "},";

    out << "\"baseline\":{";
    out << "\"healthyRuns\":"
        << summary.baseline.healthyRuns << ",";
    out << "\"healthyScans\":"
        << summary.baseline.healthyScans << ",";
    out << "\"healthyWrites\":"
        << summary.baseline.healthyWrites << ",";
    out << "\"healthyElapsedMicros\":"
        << summary.baseline.healthyElapsedMicros << ",";
    out << "\"unavailableRuns\":"
        << summary.baseline.unavailableRuns << ",";
    out << "\"unavailableScans\":"
        << summary.baseline.unavailableScans << ",";
    out << "\"unavailableWrites\":"
        << summary.baseline.unavailableWrites << ",";
    out << "\"unavailableElapsedMicros\":"
        << summary.baseline.unavailableElapsedMicros;
    out << "},";

    out << "\"authority\":{";
    out << "\"validAccepted\":"
        << (summary.authority.validAccepted ? "true" : "false")
        << ",";
    out << "\"forgedRejected\":"
        << (summary.authority.forgedRejected ? "true" : "false")
        << ",";
    out << "\"nonMonotonicRejected\":"
        << (summary.authority.nonMonotonicRejected ? "true" : "false")
        << ",";
    out << "\"conflictingBatchRejectedAtomically\":"
        << (
            summary.authority.conflictingBatchRejectedAtomically
                ? "true"
                : "false"
        );
    out << "},";

    out << "\"resources\":{";
    out << "\"replicaSizeBytes\":" << summary.replicaSize << ",";
    out << "\"coreDataBytesProxy\":"
        << summary.coreDataBytesProxy << ",";
    out << "\"coreDataGateBytes\":262144,";
    out << "\"withinLowMemoryGate\":"
        << (
            summary.coreDataBytesProxy <= 262144U
                ? "true"
                : "false"
        )
        << ",";
    out << "\"suiteElapsedMicros\":"
        << summary.totalElapsedMicros;
    out << "},";

    out << "\"antiCheat\":{";
    out << "\"localRelationInput\":\"LocalView-only\",";
    out << "\"quickJs\":false,";
    out << "\"webView\":false,";
    out << "\"androidUiInCore\":false,";
    out << "\"namedPrincipalAuthorization\":false";
    out << "}";

    out << "}";
    return out.str();
}

SuiteSummary runSuite() {
    const auto suiteStarted = std::chrono::steady_clock::now();

    SuiteSummary summary;
    summary.replicaSize = sizeof(Replica);

    Scheduler proxyScheduler(
        static_cast<std::size_t>(kWidth) *
        static_cast<std::size_t>(kHeight)
    );
    Mesh proxyMesh(kWidth, kHeight, 1U);
    summary.coreDataBytesProxy =
        proxyMesh.dataBytesProxy() +
        proxyScheduler.bytesProxy();

    const std::array<Schedule, 3> schedules = {
        Schedule::Forward,
        Schedule::Reverse,
        Schedule::Random
    };

    for (
        std::size_t scheduleIndex = 0U;
        scheduleIndex < schedules.size();
        ++scheduleIndex
    ) {
        const Schedule schedule = schedules[scheduleIndex];

        for (const std::uint32_t seed : kSeeds) {
            const std::uint8_t payload =
                payloadFor(seed, static_cast<int>(scheduleIndex));

            auto initialPair = runInitial(seed, schedule, payload);
            Mesh mesh = std::move(initialPair.first);
            const EvolutionResult initialResult = initialPair.second;

            const bool initialPass =
                initialResult.converged &&
                initialResult.metric.agreement >= kAgreementGate &&
                initialResult.metric.informed >= kInformedGate &&
                initialResult.metric.conflicts == 0;
            addEvolution(
                summary.initial,
                initialResult,
                initialPass
            );

            ProtectedSourceBoundary boundary(
                static_cast<std::uint64_t>(seed) ^
                0x55aa55aaULL ^
                static_cast<std::uint64_t>(scheduleIndex)
            );
            const SourceCapability token = boundary.issue();

            // Mesh already contains version 1 from the independent initial run.
            const std::uint8_t updatedPayload =
                static_cast<std::uint8_t>(payload ^ 0xa5U);

            const PublishResult updatePublish = boundary.publish(
                mesh,
                token,
                PublishRequest{2, updatedPayload}
            );

            EvolutionResult updateResult;
            if (updatePublish.ok) {
                updateResult = evolve(
                    mesh,
                    schedule,
                    seed ^ scheduleSalt(schedule) ^ 0xdeadbeefU,
                    kMaxSweeps
                );
            }

            const bool updatePass =
                updatePublish.ok &&
                updateResult.converged &&
                updateResult.metric.agreement >= kAgreementGate &&
                updateResult.metric.informed >= kInformedGate &&
                updateResult.metric.sourceVersion == 2 &&
                updateResult.metric.sourceData == updatedPayload;
            addEvolution(
                summary.updates,
                updateResult,
                updatePass
            );

            const std::array<double, 3> fractions = {
                0.10, 0.25, 0.50
            };

            for (
                std::size_t fractionIndex = 0U;
                fractionIndex < fractions.size();
                ++fractionIndex
            ) {
                Mesh damaged = mesh;
                damaged.resetFraction(
                    fractions[fractionIndex],
                    seed ^
                    0x70000000U ^
                    static_cast<std::uint32_t>(
                        fractionIndex * 0x10101U
                    )
                );

                const EvolutionResult repairResult = evolve(
                    damaged,
                    schedule,
                    seed ^
                    scheduleSalt(schedule) ^
                    0x71000000U ^
                    static_cast<std::uint32_t>(
                        fractionIndex * 0x1001U
                    ),
                    kMaxSweeps
                );

                const bool repairPass =
                    repairResult.converged &&
                    repairResult.metric.agreement >= kAgreementGate &&
                    repairResult.metric.informed >= kInformedGate &&
                    repairResult.metric.sourceVersion == 2 &&
                    repairResult.metric.sourceData == updatedPayload;
                addEvolution(
                    summary.repairs,
                    repairResult,
                    repairPass
                );
            }

            Mesh reset = mesh;
            reset.resetFraction(
                1.0,
                seed ^ 0x72000000U
            );
            const EvolutionResult resetResult = evolve(
                reset,
                schedule,
                seed ^ scheduleSalt(schedule) ^ 0x73000000U,
                kMaxSweeps
            );
            const bool resetPass =
                resetResult.converged &&
                resetResult.metric.agreement >= kAgreementGate &&
                resetResult.metric.informed >= kInformedGate &&
                resetResult.metric.sourceVersion == 2 &&
                resetResult.metric.sourceData == updatedPayload;
            addEvolution(
                summary.fullReset,
                resetResult,
                resetPass
            );

            // N0-E5 deterministic replay.
            auto replayA = runInitial(
                seed ^ 0x44440000U,
                schedule,
                payload
            );
            auto replayB = runInitial(
                seed ^ 0x44440000U,
                schedule,
                payload
            );

            const bool replayPass =
                replayA.second.converged &&
                replayB.second.converged &&
                replayA.first.sameState(replayB.first) &&
                replayA.second.signature == replayB.second.signature &&
                replayA.second.sweeps == replayB.second.sweeps &&
                replayA.second.localAttempts ==
                    replayB.second.localAttempts &&
                replayA.second.localChanges ==
                    replayB.second.localChanges;

            addEvolution(
                summary.replay,
                replayA.second,
                replayPass
            );
        }
    }

    // N0-E4 and healthy baseline accounting: one run per seed.
    for (const std::uint32_t seed : kSeeds) {
        const std::uint8_t payload = payloadFor(seed, 7);

        Mesh relational(kWidth, kHeight, seed ^ 0x81000000U);
        ProtectedSourceBoundary relationalBoundary(
            static_cast<std::uint64_t>(seed) ^ 0x81111111ULL
        );
        const SourceCapability relationalToken =
            relationalBoundary.issue();
        const PublishResult relationalPublish =
            relationalBoundary.publish(
                relational,
                relationalToken,
                PublishRequest{1, payload}
            );

        EvolutionResult relationalInitial;
        if (relationalPublish.ok) {
            relationalInitial = evolve(
                relational,
                Schedule::Random,
                seed ^ 0x81222222U,
                kMaxSweeps
            );
        }

        Mesh centralized(kWidth, kHeight, seed ^ 0x81000000U);
        centralized.setSource(1, payload);
        BaselineController controller{1, payload, true};

        const BaselineResult initialBroadcast =
            baselineBroadcast(centralized, controller);
        ++summary.baseline.healthyRuns;
        summary.baseline.healthyScans += initialBroadcast.scans;
        summary.baseline.healthyWrites += initialBroadcast.writes;
        summary.baseline.healthyElapsedMicros +=
            initialBroadcast.elapsedMicros;

        Mesh relationalDamaged = relational;
        Mesh centralHealthy = centralized;
        Mesh centralUnavailable = centralized;

        const std::uint32_t damageSeed = seed ^ 0x81333333U;
        relationalDamaged.resetFraction(0.25, damageSeed);
        centralHealthy.resetFraction(0.25, damageSeed);
        centralUnavailable.resetFraction(0.25, damageSeed);

        const EvolutionResult relationalRepair = evolve(
            relationalDamaged,
            Schedule::Random,
            seed ^ 0x81444444U,
            kMaxSweeps
        );

        const BaselineResult healthyRepair =
            baselineBroadcast(centralHealthy, controller);
        ++summary.baseline.healthyRuns;
        summary.baseline.healthyScans += healthyRepair.scans;
        summary.baseline.healthyWrites += healthyRepair.writes;
        summary.baseline.healthyElapsedMicros +=
            healthyRepair.elapsedMicros;

        controller.available = false;
        const BaselineResult unavailableRepair =
            baselineBroadcast(centralUnavailable, controller);
        ++summary.baseline.unavailableRuns;
        summary.baseline.unavailableScans +=
            unavailableRepair.scans;
        summary.baseline.unavailableWrites +=
            unavailableRepair.writes;
        summary.baseline.unavailableElapsedMicros +=
            unavailableRepair.elapsedMicros;

        const Metric centralUnavailableMetric =
            measure(centralUnavailable);

        ++summary.controllerLossRuns;

        const bool advantage =
            relationalInitial.converged &&
            relationalRepair.converged &&
            relationalRepair.metric.agreement >= kAgreementGate &&
            !unavailableRepair.available &&
            unavailableRepair.scans == 0U &&
            unavailableRepair.writes == 0U &&
            centralUnavailableMetric.agreement < kAgreementGate;

        if (advantage) {
            ++summary.controllerLossAdvantages;
        }
    }

    summary.authority = runAuthorityChecks();

    const bool aggregatesPass =
        summary.initial.runs == 18 &&
        summary.initial.passed == 18 &&
        summary.updates.runs == 18 &&
        summary.updates.passed == 18 &&
        summary.repairs.runs == 54 &&
        summary.repairs.passed == 54 &&
        summary.fullReset.runs == 18 &&
        (
            static_cast<double>(summary.fullReset.passed) /
            static_cast<double>(summary.fullReset.runs)
        ) >= 0.90 &&
        summary.replay.runs == 18 &&
        summary.replay.passed == 18;

    const bool controllerPass =
        summary.controllerLossRuns ==
            static_cast<int>(kSeeds.size()) &&
        summary.controllerLossAdvantages ==
            summary.controllerLossRuns;

    const bool resourcesPass =
        summary.coreDataBytesProxy <= 262144U;

    summary.pass =
        aggregatesPass &&
        controllerPass &&
        resourcesPass &&
        authorityPass(summary.authority);

    const auto suiteEnded = std::chrono::steady_clock::now();
    summary.totalElapsedMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            suiteEnded - suiteStarted
        ).count()
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

}  // namespace codynex::n0
