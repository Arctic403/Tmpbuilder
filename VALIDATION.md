# Tmpbuilder N2 Validation

A CI run is valid only if every gate below passes.

## Host configure / compile

CMake must configure and compile `codynex_n2_host` under:

- C++17
- `-Wall`
- `-Wextra`
- `-Wpedantic`
- `-Werror`

Compiler strictness may not be weakened.

## Host N2 suite

The executable must:

- exit 0;
- emit valid JSON;
- report `"pass": true`;
- report `"stageComplete": false`.

The host suite covers:

- graph generation/execution;
- tape generation/execution;
- graph/tape substitution;
- graph -> tape reconstruction;
- tape -> graph reconstruction;
- evidence-erasure fallback;
- checkpoint cross-representation restore;
- checkpoint corruption/decoy rejection;
- authority isolation;
- resource gates.

## Android build

Pinned:

- JDK 17
- Gradle 9.5.0
- AGP 9.3.0
- compileSdk / targetSdk 36
- minSdk 26
- Build Tools 36.0.0
- NDK 28.2.13676358
- CMake 3.22.1

`:app:assembleDebug` must succeed.

## ABI packaging

APK must contain:

- `lib/arm64-v8a/libcodynex_n2.so`
- `lib/armeabi-v7a/libcodynex_n2.so`

ARM32 may not be dropped to make the build green.

## ELF verification

- ARM64 => AArch64
- ARM32 => ELF32 + ARM

## Evidence bundle

Must contain:

- N2 debug APK
- both extracted native libraries
- host JSON
- ELF reports
- APK contents
- SHA-256 manifest

## Failure policy

Do not:

- skip host tests;
- weaken `-Werror`;
- remove ARM32;
- serialize generated machinery to satisfy checkpoint tests;
- persist source authority;
- change `stageComplete` to true in CI;
- claim N2 complete from host/APK success alone.

## Real-device boundary

After green CI, the APK must still prove:

- Android in-process suite PASS;
- Graph -> Tape real process restart PASS;
- Tape -> Graph real process restart PASS.

Until those are recorded, N2 remains ACTIVE.
