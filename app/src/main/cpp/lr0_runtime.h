#pragma once

#include "cxe_format.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace codynex::lr0 {

struct ActivationResult {
    bool ok = false;
    bool firstActivation = false;
    bool persistentStatePreserved = false;
    std::size_t preservedPersistentStates = 0U;
    std::uint64_t previousProgramHash = 0U;
    std::uint64_t activeProgramHash = 0U;
    std::uint64_t generation = 0U;
    std::string reason;
};

struct CallResult {
    bool ok = false;
    bool hasValue = false;
    std::int64_t value = 0;
    std::uint16_t functionId = 0U;
    std::uint64_t instructionsExecuted = 0U;
    std::size_t peakStackEntries = 0U;
    std::size_t stateWrites = 0U;
    std::string reason;
};

struct RuntimeSnapshot {
    bool hasActiveProgram = false;
    std::uint64_t activeProgramHash = 0U;
    std::uint64_t schemaFingerprint = 0U;
    std::uint64_t generation = 0U;
    std::size_t activeProgramBytes = 0U;
    std::size_t stateEntries = 0U;
    std::size_t persistentStateBytes = 0U;
};

struct PersistentStateEntry {
    std::uint16_t id = 0U;
    ValueType type = ValueType::I64;
    std::int64_t value = 0;
};

struct PersistentStateImage {
    std::uint64_t programHash = 0U;
    std::uint64_t schemaFingerprint = 0U;
    std::vector<PersistentStateEntry> entries;
};

class LiveRuntime {
public:
    ActivationResult activateBytes(
        const std::vector<std::uint8_t>& bytes
    );

    ActivationResult activateFile(
        const std::string& path
    );

    CallResult call(std::uint16_t functionId);

    bool readState(
        std::uint16_t stateId,
        std::int64_t& out
    ) const;

    bool exportPersistentState(
        PersistentStateImage& out,
        std::string& reason
    ) const;

    bool restorePersistentState(
        const PersistentStateImage& image,
        std::string& reason
    );

    bool hasActiveProgram() const;
    RuntimeSnapshot snapshot() const;

private:
    bool persistentSchemasCompatible(
        const ProgramImage& current,
        const ProgramImage& candidate,
        std::size_t& persistentCount
    ) const;

    ProgramImage activeProgram_;
    std::vector<std::int64_t> stateValues_;
    bool hasActiveProgram_ = false;
    std::uint64_t generation_ = 0U;
};

}  // namespace codynex::lr0
