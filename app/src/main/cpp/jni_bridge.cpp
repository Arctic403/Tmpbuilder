#include "lr0_runtime.h"
#include "lr0_store.h"

#include <jni.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using codynex::lr0::ActivationResult;
using codynex::lr0::CallResult;
using codynex::lr0::LiveRuntime;
using codynex::lr0::PersistentStateImage;
using codynex::lr0::RecoveryStore;
using codynex::lr0::RuntimeSnapshot;
using codynex::lr0::StoreResult;

std::mutex gMutex;
LiveRuntime gRuntime;
std::vector<std::uint8_t> gActiveProgramBytes;
std::unique_ptr<RecoveryStore> gStore;

std::string toUtf8(JNIEnv* env, jstring value) {
    if (value == nullptr) {
        return {};
    }

    const char* chars =
        env->GetStringUTFChars(value, nullptr);

    if (chars == nullptr) {
        return {};
    }

    std::string out(chars);
    env->ReleaseStringUTFChars(value, chars);
    return out;
}

std::string escapeJson(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 16U);

    for (const char ch : value) {
        switch (ch) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(ch);
                break;
        }
    }

    return out;
}

jstring toJString(
    JNIEnv* env,
    const std::string& value
) {
    return env->NewStringUTF(value.c_str());
}

std::string boolJson(bool value) {
    return value ? "true" : "false";
}

std::string snapshotJson(
    const RuntimeSnapshot& snapshot
) {
    std::ostringstream out;
    out
        << "{"
        << "\"hasActiveProgram\":"
        << boolJson(snapshot.hasActiveProgram)
        << ",\"activeProgramHash\":"
        << snapshot.activeProgramHash
        << ",\"schemaFingerprint\":"
        << snapshot.schemaFingerprint
        << ",\"generation\":"
        << snapshot.generation
        << ",\"activeProgramBytes\":"
        << snapshot.activeProgramBytes
        << ",\"stateEntries\":"
        << snapshot.stateEntries
        << ",\"persistentStateBytes\":"
        << snapshot.persistentStateBytes
        << "}";
    return out.str();
}

bool exportState(
    PersistentStateImage& state,
    std::string& reason
) {
    return gRuntime.exportPersistentState(state, reason);
}

std::string rollbackReason(
    const std::string& prefix,
    const std::string& reason
) {
    return
        prefix +
        ":" +
        reason;
}

}  // namespace

extern "C"
JNIEXPORT jstring JNICALL
Java_com_codynex_lr0lab_MainActivity_nativeOpen(
    JNIEnv* env,
    jobject,
    jstring rootDirectory
) {
    std::lock_guard<std::mutex> lock(gMutex);

    const std::string root = toUtf8(env, rootDirectory);

    if (root.empty()) {
        return toJString(
            env,
            "{\"pass\":false,\"reason\":\"root-directory-empty\"}"
        );
    }

    gRuntime = LiveRuntime{};
    gActiveProgramBytes.clear();

    try {
        gStore = std::make_unique<RecoveryStore>(root);
    } catch (...) {
        gStore.reset();
        return toJString(
            env,
            "{\"pass\":false,\"reason\":\"store-allocation-failed\"}"
        );
    }

    const StoreResult recovered =
        gStore->recover(
            gRuntime,
            gActiveProgramBytes
        );

    if (!recovered.ok) {
        if (recovered.reason == "recovery-empty") {
            return toJString(
                env,
                "{\"pass\":true,\"status\":\"empty\","
                "\"recovered\":false,\"snapshot\":" +
                    snapshotJson(gRuntime.snapshot()) +
                "}"
            );
        }

        return toJString(
            env,
            "{\"pass\":false,\"status\":\"recovery-failed\","
            "\"reason\":\"" +
                escapeJson(recovered.reason) +
            "\"}"
        );
    }

    return toJString(
        env,
        "{\"pass\":true,\"status\":\"recovered\","
        "\"recovered\":true,"
        "\"fallbackUsed\":" +
            boolJson(recovered.fallbackUsed) +
        ",\"slot\":" +
            std::to_string(recovered.activeSlot) +
        ",\"snapshot\":" +
            snapshotJson(gRuntime.snapshot()) +
        "}"
    );
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_codynex_lr0lab_MainActivity_nativeActivateCandidate(
    JNIEnv* env,
    jobject,
    jstring candidatePath
) {
    std::lock_guard<std::mutex> lock(gMutex);

    if (!gStore) {
        return toJString(
            env,
            "{\"pass\":false,\"reason\":\"runtime-not-open\"}"
        );
    }

    const std::string path = toUtf8(env, candidatePath);
    const std::vector<std::uint8_t> bytes =
        codynex::lr0::readExecutableFile(path);

    if (bytes.empty()) {
        return toJString(
            env,
            "{\"pass\":false,\"reason\":\"candidate-read-failed\"}"
        );
    }

    LiveRuntime beforeRuntime;

    try {
        beforeRuntime = gRuntime;
    } catch (...) {
        return toJString(
            env,
            "{\"pass\":false,\"reason\":\"activation-snapshot-failed\"}"
        );
    }

    const ActivationResult activation =
        gRuntime.activateBytes(bytes);

    if (!activation.ok) {
        return toJString(
            env,
            "{\"pass\":false,\"status\":\"candidate-rejected\","
            "\"reason\":\"" +
                escapeJson(activation.reason) +
            "\",\"snapshot\":" +
                snapshotJson(gRuntime.snapshot()) +
            "}"
        );
    }

    PersistentStateImage state;
    std::string stateReason;

    if (!exportState(state, stateReason)) {
        gRuntime = std::move(beforeRuntime);
        return toJString(
            env,
            "{\"pass\":false,\"status\":\"activation-rolled-back\","
            "\"reason\":\"" +
                escapeJson(
                    rollbackReason(
                        "persistent-export-failed",
                        stateReason
                    )
                ) +
            "\"}"
        );
    }

    const StoreResult stored =
        gStore->commit(bytes, state);

    if (!stored.ok) {
        gRuntime = std::move(beforeRuntime);
        return toJString(
            env,
            "{\"pass\":false,\"status\":\"activation-rolled-back\","
            "\"reason\":\"" +
                escapeJson(stored.reason) +
            "\",\"snapshot\":" +
                snapshotJson(gRuntime.snapshot()) +
            "}"
        );
    }

    gActiveProgramBytes = bytes;

    return toJString(
        env,
        "{\"pass\":true,\"status\":\"activated\","
        "\"firstActivation\":" +
            boolJson(activation.firstActivation) +
        ",\"persistentStatePreserved\":" +
            boolJson(activation.persistentStatePreserved) +
        ",\"preservedPersistentStates\":" +
            std::to_string(
                activation.preservedPersistentStates
            ) +
        ",\"slot\":" +
            std::to_string(stored.activeSlot) +
        ",\"snapshot\":" +
            snapshotJson(gRuntime.snapshot()) +
        "}"
    );
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_codynex_lr0lab_MainActivity_nativeCall(
    JNIEnv* env,
    jobject,
    jint functionId
) {
    std::lock_guard<std::mutex> lock(gMutex);

    if (!gStore) {
        return toJString(
            env,
            "{\"pass\":false,\"reason\":\"runtime-not-open\"}"
        );
    }

    if (
        functionId < 0 ||
        functionId > 65535
    ) {
        return toJString(
            env,
            "{\"pass\":false,\"reason\":\"function-id-out-of-range\"}"
        );
    }

    LiveRuntime beforeRuntime;

    try {
        beforeRuntime = gRuntime;
    } catch (...) {
        return toJString(
            env,
            "{\"pass\":false,\"reason\":\"call-snapshot-failed\"}"
        );
    }

    const CallResult call =
        gRuntime.call(
            static_cast<std::uint16_t>(functionId)
        );

    if (!call.ok) {
        return toJString(
            env,
            "{\"pass\":false,\"status\":\"call-failed\","
            "\"reason\":\"" +
                escapeJson(call.reason) +
            "\",\"instructions\":" +
                std::to_string(call.instructionsExecuted) +
            ",\"stateWrites\":" +
                std::to_string(call.stateWrites) +
            ",\"snapshot\":" +
                snapshotJson(gRuntime.snapshot()) +
            "}"
        );
    }

    PersistentStateImage state;
    std::string stateReason;

    if (!exportState(state, stateReason)) {
        gRuntime = std::move(beforeRuntime);
        return toJString(
            env,
            "{\"pass\":false,\"status\":\"call-rolled-back\","
            "\"reason\":\"" +
                escapeJson(
                    rollbackReason(
                        "persistent-export-failed",
                        stateReason
                    )
                ) +
            "\"}"
        );
    }

    if (gActiveProgramBytes.empty()) {
        gRuntime = std::move(beforeRuntime);
        return toJString(
            env,
            "{\"pass\":false,\"status\":\"call-rolled-back\","
            "\"reason\":\"active-program-bytes-missing\"}"
        );
    }

    const StoreResult stored =
        gStore->commit(
            gActiveProgramBytes,
            state
        );

    if (!stored.ok) {
        gRuntime = std::move(beforeRuntime);
        return toJString(
            env,
            "{\"pass\":false,\"status\":\"call-rolled-back\","
            "\"reason\":\"" +
                escapeJson(stored.reason) +
            "\",\"snapshot\":" +
                snapshotJson(gRuntime.snapshot()) +
            "}"
        );
    }

    std::ostringstream out;
    out
        << "{"
        << "\"pass\":true"
        << ",\"status\":\"call-committed\""
        << ",\"functionId\":" << functionId
        << ",\"hasValue\":" << boolJson(call.hasValue);

    if (call.hasValue) {
        out << ",\"value\":" << call.value;
    }

    out
        << ",\"instructions\":"
        << call.instructionsExecuted
        << ",\"peakStack\":"
        << call.peakStackEntries
        << ",\"stateWrites\":"
        << call.stateWrites
        << ",\"slot\":"
        << stored.activeSlot
        << ",\"snapshot\":"
        << snapshotJson(gRuntime.snapshot())
        << "}";

    return toJString(env, out.str());
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_codynex_lr0lab_MainActivity_nativeSnapshot(
    JNIEnv* env,
    jobject
) {
    std::lock_guard<std::mutex> lock(gMutex);

    return toJString(
        env,
        "{\"pass\":true,\"snapshot\":" +
            snapshotJson(gRuntime.snapshot()) +
        "}"
    );
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_codynex_lr0lab_MainActivity_nativeReadState(
    JNIEnv* env,
    jobject,
    jint stateId
) {
    std::lock_guard<std::mutex> lock(gMutex);

    if (
        stateId < 0 ||
        stateId > 65535
    ) {
        return toJString(
            env,
            "{\"pass\":false,\"reason\":\"state-id-out-of-range\"}"
        );
    }

    std::int64_t value = 0;

    if (
        !gRuntime.readState(
            static_cast<std::uint16_t>(stateId),
            value
        )
    ) {
        return toJString(
            env,
            "{\"pass\":false,\"reason\":\"state-unavailable\"}"
        );
    }

    return toJString(
        env,
        "{\"pass\":true,\"stateId\":" +
            std::to_string(stateId) +
        ",\"value\":" +
            std::to_string(value) +
        "}"
    );
}
