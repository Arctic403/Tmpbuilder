#include "lr0_runtime.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace codynex::lr0 {
namespace {

bool readU16FromCode(
    const std::vector<std::uint8_t>& code,
    std::size_t& pc,
    std::size_t end,
    std::uint16_t& out
) {
    if (pc + 2U > end || end > code.size()) {
        return false;
    }

    out =
        static_cast<std::uint16_t>(code[pc]) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(code[pc + 1U]) << 8U
        );

    pc += 2U;
    return true;
}

bool readI64FromCode(
    const std::vector<std::uint8_t>& code,
    std::size_t& pc,
    std::size_t end,
    std::int64_t& out
) {
    if (pc + 8U > end || end > code.size()) {
        return false;
    }

    std::uint64_t raw = 0U;

    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        raw |=
            static_cast<std::uint64_t>(code[pc++])
            << shift;
    }

    static_assert(
        sizeof(raw) == sizeof(out),
        "LR0 requires 64-bit integer storage"
    );

    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

bool addChecked(
    std::int64_t left,
    std::int64_t right,
    std::int64_t& out
) {
    if (
        right > 0 &&
        left >
            std::numeric_limits<std::int64_t>::max() - right
    ) {
        return false;
    }

    if (
        right < 0 &&
        left <
            std::numeric_limits<std::int64_t>::min() - right
    ) {
        return false;
    }

    out = left + right;
    return true;
}

std::size_t countPersistentStates(
    const ProgramImage& program
) {
    std::size_t count = 0U;

    for (const StateDecl& state : program.states) {
        if (state.persistent) {
            ++count;
        }
    }

    return count;
}

}  // namespace

ActivationResult LiveRuntime::activateBytes(
    const std::vector<std::uint8_t>& bytes
) {
    ActivationResult activation;
    activation.previousProgramHash =
        hasActiveProgram_
            ? activeProgram_.executableHash
            : 0U;

    DecodeResult decoded =
        decodeAndValidateExecutable(bytes);

    if (!decoded.ok) {
        activation.reason = decoded.reason;
        activation.activeProgramHash =
            activation.previousProgramHash;
        activation.generation = generation_;
        return activation;
    }

    std::vector<std::int64_t> candidateState;

    try {
        candidateState.resize(decoded.program.states.size());
    } catch (const std::bad_alloc&) {
        activation.reason = "candidate-state-allocation-failed";
        activation.activeProgramHash =
            activation.previousProgramHash;
        activation.generation = generation_;
        return activation;
    }

    for (
        std::size_t i = 0U;
        i < decoded.program.states.size();
        ++i
    ) {
        candidateState[i] =
            decoded.program.states[i].initialI64;
    }

    std::size_t preservedPersistentStates = 0U;

    if (hasActiveProgram_) {
        if (
            !persistentSchemasCompatible(
                activeProgram_,
                decoded.program,
                preservedPersistentStates
            )
        ) {
            activation.reason =
                "persistent-schema-incompatible";
            activation.activeProgramHash =
                activeProgram_.executableHash;
            activation.generation = generation_;
            return activation;
        }

        for (
            std::size_t i = 0U;
            i < decoded.program.states.size();
            ++i
        ) {
            const StateDecl& state =
                decoded.program.states[i];

            if (!state.persistent) {
                continue;
            }

            if (i >= stateValues_.size()) {
                activation.reason =
                    "persistent-state-source-missing";
                activation.activeProgramHash =
                    activeProgram_.executableHash;
                activation.generation = generation_;
                return activation;
            }

            candidateState[i] = stateValues_[i];
        }
    }

    const bool firstActivation = !hasActiveProgram_;
    const std::uint64_t newProgramHash =
        decoded.program.executableHash;

    activeProgram_ = std::move(decoded.program);
    stateValues_ = std::move(candidateState);
    hasActiveProgram_ = true;
    ++generation_;

    activation.ok = true;
    activation.firstActivation = firstActivation;
    activation.preservedPersistentStates =
        firstActivation
            ? 0U
            : preservedPersistentStates;
    activation.persistentStatePreserved =
        !firstActivation &&
        preservedPersistentStates > 0U;
    activation.activeProgramHash = newProgramHash;
    activation.generation = generation_;
    activation.reason =
        firstActivation
            ? "activated-initial-program"
            : "activated-replacement-program";

    return activation;
}

ActivationResult LiveRuntime::activateFile(
    const std::string& path
) {
    const std::vector<std::uint8_t> bytes =
        readExecutableFile(path);

    if (bytes.empty()) {
        ActivationResult activation;
        activation.previousProgramHash =
            hasActiveProgram_
                ? activeProgram_.executableHash
                : 0U;
        activation.activeProgramHash =
            activation.previousProgramHash;
        activation.generation = generation_;
        activation.reason = "program-file-read-failed";
        return activation;
    }

    return activateBytes(bytes);
}

CallResult LiveRuntime::call(
    std::uint16_t functionId
) {
    CallResult result;
    result.functionId = functionId;

    if (!hasActiveProgram_) {
        result.reason = "no-active-program";
        return result;
    }

    if (
        static_cast<std::size_t>(functionId) >=
        activeProgram_.functions.size()
    ) {
        result.reason = "function-id-invalid";
        return result;
    }

    const FunctionDecl& function =
        activeProgram_.functions[
            static_cast<std::size_t>(functionId)
        ];

    std::vector<std::int64_t> workingState;
    std::vector<std::int64_t> stack;

    try {
        workingState = stateValues_;
        stack.reserve(
            std::max<std::size_t>(
                function.validatedPeakStack,
                1U
            )
        );
    } catch (const std::bad_alloc&) {
        result.reason = "execution-allocation-failed";
        return result;
    }

    const std::size_t begin =
        static_cast<std::size_t>(function.codeOffset);
    const std::size_t end =
        begin +
        static_cast<std::size_t>(function.codeLength);

    if (
        begin > activeProgram_.code.size() ||
        end > activeProgram_.code.size() ||
        begin > end
    ) {
        result.reason = "active-function-range-invalid";
        return result;
    }

    std::size_t pc = begin;

    while (pc < end) {
        if (
            result.instructionsExecuted >=
            kMaxInstructionsPerCall
        ) {
            result.reason = "instruction-limit-exceeded";
            return result;
        }

        const std::uint8_t rawOpcode =
            activeProgram_.code[pc++];

        ++result.instructionsExecuted;

        switch (static_cast<Opcode>(rawOpcode)) {
            case Opcode::ConstI64: {
                std::int64_t value = 0;

                if (
                    !readI64FromCode(
                        activeProgram_.code,
                        pc,
                        end,
                        value
                    )
                ) {
                    result.reason =
                        "const-i64-runtime-truncated";
                    return result;
                }

                if (stack.size() >= kMaxStackEntries) {
                    result.reason =
                        "stack-limit-exceeded";
                    return result;
                }

                stack.push_back(value);
                break;
            }

            case Opcode::LoadState: {
                std::uint16_t stateId = 0U;

                if (
                    !readU16FromCode(
                        activeProgram_.code,
                        pc,
                        end,
                        stateId
                    )
                ) {
                    result.reason =
                        "load-state-runtime-truncated";
                    return result;
                }

                if (
                    static_cast<std::size_t>(stateId) >=
                    workingState.size()
                ) {
                    result.reason =
                        "load-state-runtime-id-invalid";
                    return result;
                }

                if (stack.size() >= kMaxStackEntries) {
                    result.reason =
                        "stack-limit-exceeded";
                    return result;
                }

                stack.push_back(
                    workingState[
                        static_cast<std::size_t>(stateId)
                    ]
                );
                break;
            }

            case Opcode::StoreState: {
                std::uint16_t stateId = 0U;

                if (
                    !readU16FromCode(
                        activeProgram_.code,
                        pc,
                        end,
                        stateId
                    )
                ) {
                    result.reason =
                        "store-state-runtime-truncated";
                    return result;
                }

                if (
                    static_cast<std::size_t>(stateId) >=
                    workingState.size()
                ) {
                    result.reason =
                        "store-state-runtime-id-invalid";
                    return result;
                }

                if (stack.empty()) {
                    result.reason =
                        "store-state-runtime-underflow";
                    return result;
                }

                const std::int64_t value = stack.back();
                stack.pop_back();

                workingState[
                    static_cast<std::size_t>(stateId)
                ] = value;

                ++result.stateWrites;
                break;
            }

            case Opcode::AddI64: {
                if (stack.size() < 2U) {
                    result.reason =
                        "add-i64-runtime-underflow";
                    return result;
                }

                const std::int64_t right = stack.back();
                stack.pop_back();

                const std::int64_t left = stack.back();
                stack.pop_back();

                std::int64_t sum = 0;

                if (!addChecked(left, right, sum)) {
                    result.reason = "add-i64-overflow";
                    return result;
                }

                stack.push_back(sum);
                break;
            }

            case Opcode::Return: {
                if (pc != end) {
                    result.reason = "return-not-final-runtime";
                    return result;
                }

                if (stack.size() > 1U) {
                    result.reason =
                        "return-stack-not-balanced-runtime";
                    return result;
                }

                if (!stack.empty()) {
                    result.hasValue = true;
                    result.value = stack.back();
                }

                stateValues_ = std::move(workingState);
                result.ok = true;
                result.reason = "ok";
                return result;
            }

            default:
                result.reason = "unknown-opcode-runtime";
                return result;
        }

        result.peakStackEntries =
            std::max(
                result.peakStackEntries,
                stack.size()
            );
    }

    result.reason = "function-ended-without-return";
    return result;
}

bool LiveRuntime::readState(
    std::uint16_t stateId,
    std::int64_t& out
) const {
    if (
        !hasActiveProgram_ ||
        static_cast<std::size_t>(stateId) >=
            stateValues_.size()
    ) {
        return false;
    }

    out =
        stateValues_[
            static_cast<std::size_t>(stateId)
        ];

    return true;
}

bool LiveRuntime::exportPersistentState(
    PersistentStateImage& out,
    std::string& reason
) const {
    out = PersistentStateImage{};

    if (!hasActiveProgram_) {
        reason = "no-active-program";
        return false;
    }

    PersistentStateImage candidate;
    candidate.programHash = activeProgram_.executableHash;
    candidate.schemaFingerprint =
        activeProgram_.schemaFingerprint;

    try {
        candidate.entries.reserve(
            countPersistentStates(activeProgram_)
        );
    } catch (const std::bad_alloc&) {
        reason = "persistent-export-allocation-failed";
        return false;
    }

    for (const StateDecl& state : activeProgram_.states) {
        if (!state.persistent) {
            continue;
        }

        const std::size_t id =
            static_cast<std::size_t>(state.id);

        if (id >= stateValues_.size()) {
            reason = "persistent-export-state-missing";
            return false;
        }

        PersistentStateEntry entry;
        entry.id = state.id;
        entry.type = state.type;
        entry.value = stateValues_[id];
        candidate.entries.push_back(entry);
    }

    out = std::move(candidate);
    reason = "ok";
    return true;
}

bool LiveRuntime::restorePersistentState(
    const PersistentStateImage& image,
    std::string& reason
) {
    if (!hasActiveProgram_) {
        reason = "no-active-program";
        return false;
    }

    if (image.programHash != activeProgram_.executableHash) {
        reason = "persistent-program-hash-mismatch";
        return false;
    }

    if (
        image.schemaFingerprint !=
        activeProgram_.schemaFingerprint
    ) {
        reason = "persistent-schema-fingerprint-mismatch";
        return false;
    }

    const std::size_t expectedPersistent =
        countPersistentStates(activeProgram_);

    if (image.entries.size() != expectedPersistent) {
        reason = "persistent-entry-count-mismatch";
        return false;
    }

    std::vector<std::int64_t> restored;

    try {
        restored = stateValues_;
    } catch (const std::bad_alloc&) {
        reason = "persistent-restore-allocation-failed";
        return false;
    }

    std::size_t entryIndex = 0U;

    for (const StateDecl& state : activeProgram_.states) {
        if (!state.persistent) {
            continue;
        }

        if (entryIndex >= image.entries.size()) {
            reason = "persistent-entry-missing";
            return false;
        }

        const PersistentStateEntry& entry =
            image.entries[entryIndex++];

        if (
            entry.id != state.id ||
            entry.type != state.type
        ) {
            reason = "persistent-entry-schema-mismatch";
            return false;
        }

        const std::size_t id =
            static_cast<std::size_t>(state.id);

        if (id >= restored.size()) {
            reason = "persistent-restore-state-missing";
            return false;
        }

        restored[id] = entry.value;
    }

    stateValues_ = std::move(restored);
    reason = "ok";
    return true;
}

bool LiveRuntime::hasActiveProgram() const {
    return hasActiveProgram_;
}

RuntimeSnapshot LiveRuntime::snapshot() const {
    RuntimeSnapshot snapshot;
    snapshot.hasActiveProgram = hasActiveProgram_;
    snapshot.generation = generation_;

    if (!hasActiveProgram_) {
        return snapshot;
    }

    snapshot.activeProgramHash =
        activeProgram_.executableHash;
    snapshot.schemaFingerprint =
        activeProgram_.schemaFingerprint;
    snapshot.activeProgramBytes =
        activeProgram_.encodedBytes;
    snapshot.stateEntries = stateValues_.size();
    snapshot.persistentStateBytes =
        countPersistentStates(activeProgram_) *
        sizeof(std::int64_t);

    return snapshot;
}

bool LiveRuntime::persistentSchemasCompatible(
    const ProgramImage& current,
    const ProgramImage& candidate,
    std::size_t& persistentCount
) const {
    persistentCount = 0U;

    if (
        current.schemaFingerprint !=
        candidate.schemaFingerprint
    ) {
        return false;
    }

    const std::size_t currentPersistent =
        countPersistentStates(current);

    const std::size_t candidatePersistent =
        countPersistentStates(candidate);

    if (currentPersistent != candidatePersistent) {
        return false;
    }

    for (const StateDecl& candidateState : candidate.states) {
        if (!candidateState.persistent) {
            continue;
        }

        const std::size_t id =
            static_cast<std::size_t>(
                candidateState.id
            );

        if (id >= current.states.size()) {
            return false;
        }

        const StateDecl& currentState =
            current.states[id];

        if (
            !currentState.persistent ||
            currentState.id != candidateState.id ||
            currentState.type != candidateState.type
        ) {
            return false;
        }

        ++persistentCount;
    }

    return persistentCount == currentPersistent;
}

}  // namespace codynex::lr0
