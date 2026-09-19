# Validation

This temporary builder exists only to validate Codynex N0 outside the current
RiftBuild native-proof limitations.

## Required passing gates

A GitHub Actions run is valid only if all of these succeed:

1. **Host configure**
   - CMake configures `host/`.

2. **Host compile**
   - C++17.
   - `-Wall -Wextra -Wpedantic -Werror`.
   - No warning may be ignored.

3. **Host N0 suite**
   - `codynex_n0_host` exits 0.
   - emitted JSON contains `"pass": true`.

4. **Android build**
   - AGP 9.3.0.
   - Gradle 9.5.0.
   - JDK 17.
   - NDK 28.2.13676358.
   - CMake 3.22.1.
   - `:app:assembleDebug` succeeds.

5. **ABI packaging**
   - APK contains `lib/arm64-v8a/libcodynex_n0.so`.
   - APK contains `lib/armeabi-v7a/libcodynex_n0.so`.

6. **ELF verification**
   - ARM64 library reports AArch64.
   - ARM32 library reports ELF32 + ARM.

7. **Evidence**
   - APK copied into the artifact bundle.
   - both native libraries extracted from the APK.
   - host JSON retained.
   - APK contents retained.
   - SHA-256 hashes generated.
   - artifact upload succeeds.

## Failure policy

Any failed gate keeps N0 at **device validation pending**.

Do not:
- weaken warnings-as-errors;
- remove ARM32 to get a green build;
- skip host tests;
- mark a build green from APK creation alone;
- claim Android runtime/device validation from CI compile evidence.

## What a green run proves

A green run proves:
- the native C++ experiment compiles and passes on the Linux host;
- the Android NDK compiles the JNI/core for both required ABIs;
- Gradle packages both native libraries into a valid debug APK.

It does **not** prove:
- the APK installs on the target phone;
- JNI executes correctly on-device;
- device memory/timing gates pass;
- native Android runtime behavior matches the host result.

Those remain device-side N0 gates.
