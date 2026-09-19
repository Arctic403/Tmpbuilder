#include "n1_core.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace codynex::n1 {

Rng::Rng(std::uint32_t seed)
    : state_(seed == 0U ? 0x9e3779b9U : seed) {}

std::uint32_t Rng::nextU32() {
    state_ ^= state_ << 13U;
    state_ ^= state_ >> 17U;
    state_ ^= state_ << 5U;
    return state_;
}

std::size_t Rng::index(std::size_t upperExclusive) {
    if (upperExclusive == 0U) {
        return 0U;
    }

    return static_cast<std::size_t>(
        nextU32() % static_cast<std::uint32_t>(upperExclusive)
    );
}

std::uint8_t Rng::byte() {
    return static_cast<std::uint8_t>(nextU32() & 0xffU);
}

bool sameReplica(const Replica& a, const Replica& b) {
    return
        a.version == b.version &&
        a.data == b.data &&
        a.rank == b.rank &&
        a.pinned == b.pinned &&
        a.conflict == b.conflict;
}

CandidateSupport candidateSupport(const LocalView& view) {
    CandidateSupport support;

    for (std::size_t i = 0U; i < view.neighborCount; ++i) {
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

Mesh::Mesh(int width, int height, std::uint32_t seed)
    : width_(width),
      height_(height),
      seed_(seed),
      cells_(
          static_cast<std::size_t>(width) *
          static_cast<std::size_t>(height)
      ) {
    Rng rng(seed_);

    for (Replica& cellValue : cells_) {
        cellValue.version = 0;
        cellValue.data = rng.byte();
        cellValue.rank = 0;
        cellValue.pinned = false;
        cellValue.conflict = false;
    }

    if (!cells_.empty()) {
        cells_[0] = Replica{0, 0U, kRootRank, true, false};
    }
}

int Mesh::width() const {
    return width_;
}

int Mesh::height() const {
    return height_;
}

std::size_t Mesh::size() const {
    return cells_.size();
}

const Replica& Mesh::cell(std::size_t index) const {
    return cells_.at(index);
}

Replica& Mesh::cell(std::size_t index) {
    return cells_.at(index);
}

const Replica& Mesh::source() const {
    return cells_.front();
}

void Mesh::setSource(std::int32_t version, std::uint8_t data) {
    cells_.front() = Replica{
        version,
        data,
        kRootRank,
        true,
        false
    };
}

std::size_t Mesh::neighborIndices(
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

LocalView Mesh::localView(std::size_t index) const {
    LocalView view;
    view.self = cells_.at(index);

    std::array<std::size_t, 4> neighbors{};
    const std::size_t count = neighborIndices(index, neighbors);
    view.neighborCount = count;

    for (std::size_t i = 0U; i < count; ++i) {
        view.neighbors[i] = cells_.at(neighbors[i]);
    }

    return view;
}

Replica Mesh::evaluateLocal(std::size_t index) const {
    return chooseReplica(localView(index));
}

bool Mesh::applyEvaluated(
    std::size_t index,
    const Replica& evaluated
) {
    Replica& current = cells_.at(index);

    if (current.pinned) {
        return false;
    }

    Replica next = evaluated;
    next.pinned = false;
    next.rank = std::max<std::int32_t>(
        0,
        std::min<std::int32_t>(kRootRank - 1, next.rank)
    );

    if (sameReplica(current, next)) {
        return false;
    }

    current = next;
    return true;
}

bool Mesh::applyLocal(std::size_t index) {
    if (cells_.at(index).pinned) {
        return false;
    }

    return applyEvaluated(index, evaluateLocal(index));
}

bool Mesh::isActionable(std::size_t index) const {
    const Replica& current = cells_.at(index);

    if (current.pinned) {
        return false;
    }

    Replica next = evaluateLocal(index);
    next.pinned = false;
    next.rank = std::max<std::int32_t>(
        0,
        std::min<std::int32_t>(kRootRank - 1, next.rank)
    );

    return !sameReplica(current, next);
}

std::size_t Mesh::resetFraction(
    double fraction,
    std::uint32_t seed
) {
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

bool Mesh::sameState(const Mesh& other) const {
    if (
        width_ != other.width_ ||
        height_ != other.height_ ||
        cells_.size() != other.cells_.size()
    ) {
        return false;
    }

    for (std::size_t i = 0U; i < cells_.size(); ++i) {
        if (!sameReplica(cells_[i], other.cells_[i])) {
            return false;
        }
    }

    return true;
}

std::uint64_t Mesh::signature() const {
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

std::size_t Mesh::dataBytesProxy() const {
    return cells_.capacity() * sizeof(Replica);
}

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

bool guaranteeSatisfied(const Metric& metric) {
    return
        metric.agreement >= kAgreementGate &&
        metric.informed >= kInformedGate &&
        metric.conflicts == 0;
}

std::uint64_t mix64(std::uint64_t value) {
    value ^= value >> 30U;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27U;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31U;
    return value;
}

SourceCapability::SourceCapability(
    std::uint64_t id,
    std::uint64_t proof
)
    : id_(id),
      proof_(proof) {}

ProtectedSourceBoundary::ProtectedSourceBoundary(std::uint64_t seed)
    : secret_(mix64(seed ^ 0xd1b54a32d192ed03ULL)) {}

SourceCapability ProtectedSourceBoundary::issue() {
    const std::uint64_t id = nextId_++;
    return SourceCapability(id, proofFor(id));
}

bool ProtectedSourceBoundary::authorize(
    const SourceCapability& token
) const {
    return
        token.id_ != 0U &&
        token.proof_ == proofFor(token.id_);
}

PublishResult ProtectedSourceBoundary::publish(
    Mesh& mesh,
    const SourceCapability& token,
    const PublishRequest& request
) const {
    const PublishResult validation =
        validate(mesh, token, request);

    if (!validation.ok) {
        return validation;
    }

    mesh.setSource(request.version, request.data);
    return PublishResult{true, false, "ok"};
}

PublishResult ProtectedSourceBoundary::publishPairAtomic(
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

PublishResult ProtectedSourceBoundary::validate(
    const Mesh& mesh,
    const SourceCapability& token,
    const PublishRequest& request
) const {
    if (!authorize(token)) {
        return PublishResult{
            false,
            false,
            "not-authorized"
        };
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

std::uint64_t ProtectedSourceBoundary::proofFor(
    std::uint64_t id
) const {
    return mix64(
        secret_ ^
        id ^
        0x9e3779b97f4a7c15ULL
    );
}

namespace {

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

}  // namespace

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

}  // namespace codynex::n1
