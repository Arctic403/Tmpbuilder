# Tmpbuilder N1 Validation

A GitHub Actions run is valid only if every gate below passes.

## 1. Host configure

CMake must configure `host/` successfully.

## 2. Host compile

The N1 host executable must compile under:

- C++17;
- `-Wall`;
- `-Wextra`;
- `-Wpedantic`;
- `-Werror`.

Do not weaken compiler strictness to obtain a green run.

## 3. Host N1 suite

`codynex_n1_host` must:

- exit 0;
- emit valid JSON;
- contain `"pass": true`.

The suite includes dense/frontier parity, replacement, rebuild, specialization,
authority, controller-loss and resource gates.

## 4. Android build

Pinned toolchain:

- AGP 9.3.0;
- Gradle 9.5.0;
- JDK 17;
- NDK 28.2.13676358;
- CMake 3.22.1;
- compileSdk/targetSdk 36;
- minSdk 26.

`:app:assembleDebug` must succeed.

## 5. ABI packaging

APK must contain:

- `lib/arm64-v8a/libcodynex_n1.so`;
- `lib/armeabi-v7a/libcodynex_n1.so`.

ARM32 may not be removed to make the build pass.

## 6. ELF verification

- ARM64 library: AArch64;
- ARM32 library: ELF32 + ARM.

## 7. Evidence

The uploaded bundle must contain:

- N1 debug APK;
- both extracted native libraries;
- host N1 JSON;
- ELF reports;
- APK contents;
- SHA-256 manifest.

## Failure policy

A failed gate remains failed.

Do not:

- skip host N1 tests;
- hide profile/rebuild work;
- remove frontier/dense replacement tests;
- relax warnings-as-errors;
- remove ARM32;
- claim N1 complete from APK creation alone.

## Green CI meaning

A green CI run proves:

- the N1 native experiment compiles on the host;
- the host N1 falsification suite passes;
- Android NDK compiles N1 for both required ARM ABIs;
- the APK packages both libraries correctly.

Real-device JNI execution remains mandatory before the Codynex roadmap may mark
N1 complete.
