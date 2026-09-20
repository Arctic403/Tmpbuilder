#include "n2_suite.h"
#include "n2_validation.h"

#include <jni.h>

#include <string>

namespace {

std::string toUtf8(JNIEnv* env, jstring value) {
    if (value == nullptr) {
        return {};
    }

    const char* chars = env->GetStringUTFChars(value, nullptr);

    if (chars == nullptr) {
        return {};
    }

    std::string out(chars);
    env->ReleaseStringUTFChars(value, chars);
    return out;
}

codynex::n2::PlanRepresentation representationFromInt(jint value) {
    return value == 2
        ? codynex::n2::PlanRepresentation::Tape
        : codynex::n2::PlanRepresentation::Graph;
}

jstring toJString(
    JNIEnv* env,
    const codynex::n2::RunResult& result
) {
    return env->NewStringUTF(result.json.c_str());
}

}  // namespace

extern "C"
JNIEXPORT jstring JNICALL
Java_com_codynex_n2lab_MainActivity_nativeRunAll(
    JNIEnv* env,
    jobject
) {
    return toJString(
        env,
        codynex::n2::runValidatedHostAndInProcessSuite()
    );
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_codynex_n2lab_MainActivity_nativePrepareCold(
    JNIEnv* env,
    jobject,
    jstring path,
    jint preRepresentation,
    jboolean damageCase
) {
    return toJString(
        env,
        codynex::n2::prepareColdCheckpointProof(
            toUtf8(env, path),
            representationFromInt(preRepresentation),
            damageCase == JNI_TRUE
        )
    );
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_codynex_n2lab_MainActivity_nativeResumeCold(
    JNIEnv* env,
    jobject,
    jstring path,
    jint postRepresentation
) {
    return toJString(
        env,
        codynex::n2::resumeColdCheckpointProof(
            toUtf8(env, path),
            representationFromInt(postRepresentation)
        )
    );
}
