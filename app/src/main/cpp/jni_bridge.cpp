#include <jni.h>

#include <string>

#include "codynex_n0.h"

extern "C"
JNIEXPORT jstring JNICALL
Java_com_codynex_n0lab_MainActivity_nativeRunAll(
    JNIEnv* env,
    jobject /* thiz */) {
    const codynex::n0::RunResult report =
        codynex::n0::runAll();

    return env->NewStringUTF(report.json.c_str());
}
