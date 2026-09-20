#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace codynex::n2 {

inline constexpr std::int32_t kRootRank = 32767;
inline constexpr double kAgreementGate = 0.99;
inline constexpr double kInformedGate = 0.99;

enum class Schedule {
    Forward,
    Reverse,
    Random
};

class Rng {
public:
    explicit Rng(std::uint32_t seed);

    std::uint32_t nextU32();
    std::size_t index(std::size_t upperExclusive);
    std::uint8_t byte();

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

bool sameReplica(const Replica& a, const Replica& b);

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

CandidateSupport candidateSupport(const LocalView& view);
Replica chooseReplica(const LocalView& view);

class Mesh {
public:
    Mesh(int width, int height, std::uint32_t seed);

    int width() const;
    int height() const;
    std::size_t size() const;

    const Replica& cell(std::size_t index) const;
    Replica& cell(std::size_t index);
    const Replica& source() const;

    void setSource(std::int32_t version, std::uint8_t data);

    std::size_t neighborIndices(
        std::size_t index,
        std::array<std::size_t, 4>& out
    ) const;

    LocalView localView(std::size_t index) const;
    Replica evaluateLocal(std::size_t index) const;
    bool applyEvaluated(std::size_t index, const Replica& evaluated);
    bool applyLocal(std::size_t index);
    bool isActionable(std::size_t index) const;

    std::size_t resetFraction(double fraction, std::uint32_t seed);

    bool sameState(const Mesh& other) const;
    std::uint64_t signature() const;
    std::size_t dataBytesProxy() const;

private:
    int width_;
    int height_;
    std::uint32_t seed_;
    std::vector<Replica> cells_;
};

struct Metric {
    double agreement = 0.0;
    double informed = 0.0;
    int conflicts = 0;
    int sourceVersion = 0;
    std::uint8_t sourceData = 0U;
};

Metric measure(const Mesh& mesh);
bool guaranteeSatisfied(const Metric& metric);

std::uint64_t mix64(std::uint64_t value);

class SourceCapability {
public:
    SourceCapability() = default;

private:
    std::uint64_t id_ = 0U;
    std::uint64_t proof_ = 0U;

    SourceCapability(std::uint64_t id, std::uint64_t proof);

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
    explicit ProtectedSourceBoundary(std::uint64_t seed);

    SourceCapability issue();
    bool authorize(const SourceCapability& token) const;

    PublishResult publish(
        Mesh& mesh,
        const SourceCapability& token,
        const PublishRequest& request
    ) const;

    PublishResult publishPairAtomic(
        Mesh& mesh,
        const SourceCapability& firstToken,
        const PublishRequest& first,
        const SourceCapability& secondToken,
        const PublishRequest& second
    ) const;

private:
    PublishResult validate(
        const Mesh& mesh,
        const SourceCapability& token,
        const PublishRequest& request
    ) const;

    std::uint64_t proofFor(std::uint64_t id) const;

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

BaselineResult baselineBroadcast(
    Mesh& mesh,
    const BaselineController& controller
);

}  // namespace codynex::n2
