#include "cxe_builder.h"
#include "../app/src/main/cpp/lr0_runtime.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using codynex::lr0::ActivationResult;
using codynex::lr0::CallResult;
using codynex::lr0::LiveRuntime;
using codynex::lr0::Opcode;
using codynex::lr0::RuntimeSnapshot;
using codynex::lr0::hosttest::FunctionCode;
using codynex::lr0::hosttest::HostStateDecl;
using codynex::lr0::hosttest::buildExecutable;
using codynex::lr0::hosttest::refreshIntegrity;
using codynex::lr0::hosttest::writeBinaryFile;

struct Summary {
    int checks = 0;
    int passed = 0;
    int malformedRuns = 0;
    int malformedRejected = 0;
    bool programAExternalFile = false;
    bool programBExternalFile = false;
    bool statePreservedAcrossReplacement = false;
    bool replacementBehaviorChanged = false;
    bool invalidReplacementKeptActive = false;
    bool incompatibleSchemaRejected = false;
    bool overflowRolledBack = false;
};

void record(Summary& summary, bool pass) {
    ++summary.checks;

    if (pass) {
        ++summary.passed;
    }
}

void writeU16At(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint16_t value
) {
    if (offset + 2U > bytes.size()) {
        return;
    }

    bytes[offset] =
        static_cast<std::uint8_t>(value & 0xffU);
    bytes[offset + 1U] =
        static_cast<std::uint8_t>(
            (value >> 8U) & 0xffU
        );
}

void writeU32At(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint32_t value
) {
    if (offset + 4U > bytes.size()) {
        return;
    }

    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
        bytes[offset++] =
            static_cast<std::uint8_t>(
                (value >> shift) & 0xffU
            );
    }
}

std::vector<std::uint8_t> makeProgram(
    std::int64_t increment
) {
    const std::vector<HostStateDecl> states = {
        {true, 0}
    };

    FunctionCode add;
    add.loadState(0U)
        .constI64(increment)
        .addI64()
        .storeState(0U)
        .loadState(0U)
        .returnValue();

    FunctionCode read;
    read.loadState(0U)
        .returnValue();

    return buildExecutable(
        states,
        {add, read}
    );
}

std::vector<std::uint8_t> makeOverflowProgram() {
    return makeProgram(
        std::numeric_limits<std::int64_t>::max()
    );
}

std::vector<std::uint8_t> makeIncompatibleProgram() {
    const std::vector<HostStateDecl> states = {
        {false, 0}
    };

    FunctionCode read;
    read.loadState(0U)
        .returnValue();

    return buildExecutable(states, {read});
}

std::vector<std::uint8_t> makeTwoStateProbe() {
    const std::vector<HostStateDecl> states = {
        {true, 0},
        {false, 0}
    };

    FunctionCode noValue;
    noValue.returnValue();

    return buildExecutable(states, {noValue});
}

bool stateEquals(
    const LiveRuntime& runtime,
    std::int64_t expected
) {
    std::int64_t value = 0;

    return runtime.readState(0U, value) &&
        value == expected;
}

bool rejectAndPreserve(
    LiveRuntime& runtime,
    const std::vector<std::uint8_t>& candidate,
    std::uint64_t expectedHash,
    std::int64_t expectedState
) {
    const ActivationResult activation =
        runtime.activateBytes(candidate);

    const RuntimeSnapshot snapshot =
        runtime.snapshot();

    return
        !activation.ok &&
        snapshot.hasActiveProgram &&
        snapshot.activeProgramHash == expectedHash &&
        stateEquals(runtime, expectedState);
}

void malformedCase(
    Summary& summary,
    LiveRuntime& runtime,
    const std::vector<std::uint8_t>& candidate,
    std::uint64_t expectedHash,
    std::int64_t expectedState
) {
    ++summary.malformedRuns;

    const bool rejected =
        rejectAndPreserve(
            runtime,
            candidate,
            expectedHash,
            expectedState
        );

    if (rejected) {
        ++summary.malformedRejected;
    }

    record(summary, rejected);
}

}  // namespace

int main() {
    Summary summary;

    const std::vector<std::uint8_t> programA =
        makeProgram(1);

    const std::vector<std::uint8_t> programB =
        makeProgram(5);

    const std::string pathA = "lr0-program-a.cxe";
    const std::string pathB = "lr0-program-b.cxe";

    summary.programAExternalFile =
        writeBinaryFile(pathA, programA);

    summary.programBExternalFile =
        writeBinaryFile(pathB, programB);

    record(
        summary,
        summary.programAExternalFile &&
        summary.programBExternalFile
    );

    LiveRuntime runtime;

    const ActivationResult activateA =
        runtime.activateFile(pathA);

    record(
        summary,
        activateA.ok &&
        activateA.firstActivation &&
        runtime.snapshot().generation == 1U &&
        stateEquals(runtime, 0)
    );

    bool aCallsPass = true;

    for (std::int64_t expected = 1; expected <= 3; ++expected) {
        const CallResult call = runtime.call(0U);

        if (
            !call.ok ||
            !call.hasValue ||
            call.value != expected ||
            !stateEquals(runtime, expected)
        ) {
            aCallsPass = false;
        }
    }

    record(summary, aCallsPass);

    const RuntimeSnapshot beforeReplacement =
        runtime.snapshot();

    const ActivationResult activateB =
        runtime.activateFile(pathB);

    summary.statePreservedAcrossReplacement =
        activateB.ok &&
        !activateB.firstActivation &&
        activateB.persistentStatePreserved &&
        activateB.preservedPersistentStates == 1U &&
        stateEquals(runtime, 3);

    record(
        summary,
        summary.statePreservedAcrossReplacement
    );

    const CallResult bCall = runtime.call(0U);
    const CallResult bRead = runtime.call(1U);

    summary.replacementBehaviorChanged =
        bCall.ok &&
        bCall.hasValue &&
        bCall.value == 8 &&
        bRead.ok &&
        bRead.hasValue &&
        bRead.value == 8 &&
        stateEquals(runtime, 8);

    record(
        summary,
        summary.replacementBehaviorChanged
    );

    const RuntimeSnapshot activeB =
        runtime.snapshot();

    record(
        summary,
        activeB.activeProgramHash !=
            beforeReplacement.activeProgramHash &&
        activeB.generation == 2U
    );

    {
        std::vector<std::uint8_t> bad = programB;
        bad[0] = 'Z';
        malformedCase(
            summary,
            runtime,
            bad,
            activeB.activeProgramHash,
            8
        );
    }

    {
        std::vector<std::uint8_t> bad = programB;
        bad[4] = 2U;
        bad[5] = 0U;
        refreshIntegrity(bad);
        malformedCase(
            summary,
            runtime,
            bad,
            activeB.activeProgramHash,
            8
        );
    }

    {
        std::vector<std::uint8_t> bad = programB;
        bad.resize(
            codynex::lr0::kMaxExecutableBytes + 1U,
            0U
        );
        malformedCase(
            summary,
            runtime,
            bad,
            activeB.activeProgramHash,
            8
        );
    }

    {
        std::vector<std::uint8_t> bad = programB;
        bad.back() ^= 0x01U;
        malformedCase(
            summary,
            runtime,
            bad,
            activeB.activeProgramHash,
            8
        );
    }

    {
        std::vector<std::uint8_t> bad =
            makeTwoStateProbe();

        writeU16At(bad, 40U, 0U);
        refreshIntegrity(bad);

        malformedCase(
            summary,
            runtime,
            bad,
            activeB.activeProgramHash,
            8
        );
    }

    {
        std::vector<std::uint8_t> bad = programB;
        bad[30U] = 0x7fU;
        refreshIntegrity(bad);

        malformedCase(
            summary,
            runtime,
            bad,
            activeB.activeProgramHash,
            8
        );
    }

    {
        std::vector<std::uint8_t> bad = programB;
        bad[31U] = 0x80U;
        refreshIntegrity(bad);

        malformedCase(
            summary,
            runtime,
            bad,
            activeB.activeProgramHash,
            8
        );
    }

    {
        std::vector<std::uint8_t> bad = programB;
        writeU16At(bad, 52U, 0U);
        refreshIntegrity(bad);

        malformedCase(
            summary,
            runtime,
            bad,
            activeB.activeProgramHash,
            8
        );
    }

    {
        std::vector<std::uint8_t> bad = programB;
        writeU32At(bad, 44U, 0xffffU);
        refreshIntegrity(bad);

        malformedCase(
            summary,
            runtime,
            bad,
            activeB.activeProgramHash,
            8
        );
    }

    {
        std::vector<std::uint8_t> bad = programB;
        writeU32At(bad, 56U, 0U);
        refreshIntegrity(bad);

        malformedCase(
            summary,
            runtime,
            bad,
            activeB.activeProgramHash,
            8
        );
    }

    {
        std::vector<std::uint8_t> bad = programB;
        bad[64U] = 0xffU;
        refreshIntegrity(bad);

        malformedCase(
            summary,
            runtime,
            bad,
            activeB.activeProgramHash,
            8
        );
    }

    {
        std::vector<std::uint8_t> bad = programB;
        bad[64U] =
            static_cast<std::uint8_t>(Opcode::AddI64);
        refreshIntegrity(bad);

        malformedCase(
            summary,
            runtime,
            bad,
            activeB.activeProgramHash,
            8
        );
    }

    summary.invalidReplacementKeptActive =
        summary.malformedRuns == 12 &&
        summary.malformedRejected == 12 &&
        runtime.snapshot().activeProgramHash ==
            activeB.activeProgramHash &&
        runtime.snapshot().generation == 2U &&
        stateEquals(runtime, 8);

    record(
        summary,
        summary.invalidReplacementKeptActive
    );

    {
        const ActivationResult incompatible =
            runtime.activateBytes(
                makeIncompatibleProgram()
            );

        summary.incompatibleSchemaRejected =
            !incompatible.ok &&
            runtime.snapshot().activeProgramHash ==
                activeB.activeProgramHash &&
            runtime.snapshot().generation == 2U &&
            stateEquals(runtime, 8);

        record(
            summary,
            summary.incompatibleSchemaRejected
        );
    }

    {
        const ActivationResult overflowActivation =
            runtime.activateBytes(
                makeOverflowProgram()
            );

        const std::uint64_t overflowHash =
            runtime.snapshot().activeProgramHash;

        const CallResult overflowCall =
            runtime.call(0U);

        summary.overflowRolledBack =
            overflowActivation.ok &&
            overflowHash !=
                activeB.activeProgramHash &&
            !overflowCall.ok &&
            overflowCall.reason == "add-i64-overflow" &&
            stateEquals(runtime, 8);

        record(
            summary,
            summary.overflowRolledBack
        );

        const ActivationResult restoreB =
            runtime.activateFile(pathB);

        record(
            summary,
            restoreB.ok &&
            restoreB.persistentStatePreserved &&
            stateEquals(runtime, 8)
        );
    }

    const RuntimeSnapshot finalSnapshot =
        runtime.snapshot();

    const bool pass =
        summary.checks == summary.passed &&
        summary.malformedRuns == 12 &&
        summary.malformedRejected == 12 &&
        summary.statePreservedAcrossReplacement &&
        summary.replacementBehaviorChanged &&
        summary.invalidReplacementKeptActive &&
        summary.incompatibleSchemaRejected &&
        summary.overflowRolledBack &&
        finalSnapshot.hasActiveProgram &&
        stateEquals(runtime, 8);

    std::cout
        << "{"
        << "\"pass\":"
        << (pass ? "true" : "false")
        << ",\"scope\":\"lr0-host-live-runtime-core\""
        << ",\"checks\":{"
        << "\"runs\":" << summary.checks
        << ",\"passed\":" << summary.passed
        << "}"
        << ",\"externalPrograms\":{"
        << "\"programAFile\":"
        << (summary.programAExternalFile ? "true" : "false")
        << ",\"programBFile\":"
        << (summary.programBExternalFile ? "true" : "false")
        << "}"
        << ",\"replacement\":{"
        << "\"statePreserved\":"
        << (
            summary.statePreservedAcrossReplacement
                ? "true"
                : "false"
        )
        << ",\"behaviorChanged\":"
        << (
            summary.replacementBehaviorChanged
                ? "true"
                : "false"
        )
        << "}"
        << ",\"invalidCandidates\":{"
        << "\"runs\":" << summary.malformedRuns
        << ",\"rejected\":"
        << summary.malformedRejected
        << ",\"activeProgramPreserved\":"
        << (
            summary.invalidReplacementKeptActive
                ? "true"
                : "false"
        )
        << "}"
        << ",\"incompatibleSchemaRejected\":"
        << (
            summary.incompatibleSchemaRejected
                ? "true"
                : "false"
        )
        << ",\"overflowStateRollback\":"
        << (
            summary.overflowRolledBack
                ? "true"
                : "false"
        )
        << ",\"final\":{"
        << "\"generation\":"
        << finalSnapshot.generation
        << ",\"programBytes\":"
        << finalSnapshot.activeProgramBytes
        << ",\"persistentStateBytes\":"
        << finalSnapshot.persistentStateBytes
        << ",\"state0\":8"
        << "}"
        << ",\"androidProofRequired\":true"
        << "}"
        << std::endl;

    return pass ? 0 : 1;
}
