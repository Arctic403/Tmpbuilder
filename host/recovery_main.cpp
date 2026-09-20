#include "cxe_builder.h"
#include "../app/src/main/cpp/lr0_store.h"

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

using codynex::lr0::CallResult;
using codynex::lr0::LiveRuntime;
using codynex::lr0::PersistentStateImage;
using codynex::lr0::RecoveryStore;
using codynex::lr0::StoreResult;
using codynex::lr0::hosttest::FunctionCode;
using codynex::lr0::hosttest::HostStateDecl;
using codynex::lr0::hosttest::buildExecutable;

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

    return buildExecutable(states, {add, read});
}

bool stateEquals(
    const LiveRuntime& runtime,
    std::int64_t expected
) {
    std::int64_t value = 0;

    return
        runtime.readState(0U, value) &&
        value == expected;
}

bool exportState(
    const LiveRuntime& runtime,
    PersistentStateImage& state
) {
    std::string reason;
    return runtime.exportPersistentState(state, reason);
}

bool corruptLastByte(const std::string& path) {
    std::fstream stream(
        path,
        std::ios::binary |
        std::ios::in |
        std::ios::out
    );

    if (!stream) {
        return false;
    }

    stream.seekg(-1, std::ios::end);

    char value = 0;
    stream.read(&value, 1);

    if (!stream) {
        return false;
    }

    value = static_cast<char>(
        static_cast<unsigned char>(value) ^ 0x01U
    );

    stream.seekp(-1, std::ios::end);
    stream.write(&value, 1);
    stream.flush();
    return static_cast<bool>(stream);
}

void cleanStore(const std::string& root) {
    std::remove((root + "/slot0.cxe").c_str());
    std::remove((root + "/slot0.state").c_str());
    std::remove((root + "/slot1.cxe").c_str());
    std::remove((root + "/slot1.state").c_str());
    std::remove((root + "/recovery.rec").c_str());
}

}  // namespace

int main() {
    const std::string root = "lr0-recovery-test";
    cleanStore(root);

    const std::vector<std::uint8_t> programA =
        makeProgram(1);

    const std::vector<std::uint8_t> programB =
        makeProgram(5);

    bool initialLiveBehavior = false;
    bool firstCommit = false;
    bool firstRecovery = false;
    bool secondCommit = false;
    bool secondRecovery = false;
    bool fallbackRecovery = false;
    int currentSlot = -1;

    RecoveryStore store(root);

    {
        LiveRuntime runtime;

        if (!runtime.activateBytes(programA).ok) {
            std::cout
                << "{\"pass\":false,"
                << "\"reason\":\"activate-a-failed\"}"
                << std::endl;
            return 1;
        }

        bool callsOk = true;

        for (
            std::int64_t expected = 1;
            expected <= 3;
            ++expected
        ) {
            const CallResult call = runtime.call(0U);

            if (
                !call.ok ||
                !call.hasValue ||
                call.value != expected
            ) {
                callsOk = false;
            }
        }

        if (!runtime.activateBytes(programB).ok) {
            std::cout
                << "{\"pass\":false,"
                << "\"reason\":\"activate-b-failed\"}"
                << std::endl;
            return 1;
        }

        const CallResult changed = runtime.call(0U);

        initialLiveBehavior =
            callsOk &&
            changed.ok &&
            changed.hasValue &&
            changed.value == 8 &&
            stateEquals(runtime, 8);

        PersistentStateImage state;

        if (exportState(runtime, state)) {
            const StoreResult committed =
                store.commit(programB, state);

            firstCommit = committed.ok;
            currentSlot = committed.activeSlot;
        }
    }

    std::vector<std::uint8_t> recoveredProgram;

    {
        LiveRuntime runtime;
        const StoreResult recovered =
            store.recover(
                runtime,
                recoveredProgram
            );

        firstRecovery =
            recovered.ok &&
            recovered.recovered &&
            !recoveredProgram.empty() &&
            stateEquals(runtime, 8);

        const CallResult call = runtime.call(0U);

        if (
            firstRecovery &&
            call.ok &&
            call.hasValue &&
            call.value == 13 &&
            stateEquals(runtime, 13)
        ) {
            PersistentStateImage state;

            if (exportState(runtime, state)) {
                const StoreResult committed =
                    store.commit(
                        recoveredProgram,
                        state
                    );

                secondCommit = committed.ok;
                currentSlot = committed.activeSlot;
            }
        }
    }

    {
        LiveRuntime runtime;
        std::vector<std::uint8_t> program;

        const StoreResult recovered =
            store.recover(runtime, program);

        secondRecovery =
            recovered.ok &&
            recovered.recovered &&
            stateEquals(runtime, 13);
    }

    if (secondCommit && currentSlot >= 0) {
        const std::string statePath =
            root +
            (
                currentSlot == 0
                    ? "/slot0.state"
                    : "/slot1.state"
            );

        if (corruptLastByte(statePath)) {
            LiveRuntime runtime;
            std::vector<std::uint8_t> program;

            const StoreResult recovered =
                store.recover(runtime, program);

            fallbackRecovery =
                recovered.ok &&
                recovered.recovered &&
                recovered.fallbackUsed &&
                stateEquals(runtime, 8);
        }
    }

    const bool pass =
        initialLiveBehavior &&
        firstCommit &&
        firstRecovery &&
        secondCommit &&
        secondRecovery &&
        fallbackRecovery;

    std::cout
        << "{"
        << "\"pass\":"
        << (pass ? "true" : "false")
        << ",\"scope\":\"lr0-recovery-store\""
        << ",\"initialLiveBehavior\":"
        << (initialLiveBehavior ? "true" : "false")
        << ",\"firstCommit\":"
        << (firstCommit ? "true" : "false")
        << ",\"firstRecovery\":"
        << (firstRecovery ? "true" : "false")
        << ",\"secondCommit\":"
        << (secondCommit ? "true" : "false")
        << ",\"secondRecovery\":"
        << (secondRecovery ? "true" : "false")
        << ",\"fallbackRecovery\":"
        << (fallbackRecovery ? "true" : "false")
        << "}"
        << std::endl;

    return pass ? 0 : 1;
}
