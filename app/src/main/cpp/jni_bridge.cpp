#include "n1_suite.h"

#include <jni.h>

extern "C"
JNIEXPORT jstring JNICALL
Java_com_codynex_n1lab_MainActivity_nativeRunAll(
    JNIEnv* env,
    jobject
) {
    const codynex::n1::RunResult report =
        codynex::n1::runAll();

    return env->NewStringUTF(report.json.c_str());
}
