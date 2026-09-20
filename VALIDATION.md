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

## Validated host N2 suite

The executable must:

- exit 0;
- emit valid JSON;
- report `"pass": true`;
- report `"stageComplete": false`.

The base suite must pass:

- graph propagation: 6/6;
- graph repair: 6/6;
- tape propagation: 6/6;
- tape repair: 6/6;
- graph/tape substitution: 6/6;
- graph -> tape reconstruction: 6/6;
- tape -> graph reconstruction: 6/6;
- evidence-erasure behavior: 6/6;
- checkpoint graph -> tape: 6/6;
- checkpoint tape -> graph: 6/6;
- checkpoint corruption rejection: 12/12;
- checkpoint decoy rejection: 12/12;
- generated machinery serialized bytes: 0;
- authority serialized bytes: 0;
- resource gates: pass.

The mandatory hardening layer must also pass:

- N1 dense-equivalent parity: 12/12;
- N1 frontier-equivalent parity: 12/12;
- dense controller-loss preservation: 6/6;
- frontier controller-loss preservation: 6/6;
- authority after graph reconstruction: 6/6;
- authority after tape reconstruction: 6/6;
- oversized checkpoint rejection: true;
- primitive catalogue authority isolation: true.

The workflow explicitly checks these JSON fields with `jq`; a base-suite-only green result is insufficient.

## Checkpoint hardening

The native checkpoint boundary must:

- reject files larger than 8192 bytes before allocation/decode;
- reject invalid schema/version/integrity;
- reject unknown flag bits;
- require exactly one pinned source at index 0;
- require source rank `kRootRank`;
- reject ordinary replicas using root rank;
- serialize zero generated-machinery bytes;
- serialize zero authority-secret bytes.

Invalid tape opcodes must fail closed.

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

- `codynex_n2_host`
- N2 debug APK
- both extracted native libraries
- validated host JSON
- ELF reports
- APK contents
- SHA-256 manifest

The SHA manifest must include the host executable as well as the APK, native libraries and host result.

## Failure policy

Do not:

- skip host tests;
- bypass the hardening wrapper;
- weaken `-Werror`;
- remove ARM32;
- serialize generated machinery to satisfy checkpoint tests;
- persist source authority;
- weaken checkpoint bounds/invariant checks;
- silently map invalid plan opcodes to a valid primitive;
- change `stageComplete` to true in CI;
- claim N2 complete from host/APK success alone.

## Real-device boundary

After green CI, the APK must still prove:

- validated Android in-process suite PASS;
- Graph -> Tape real process restart PASS;
- Tape -> Graph real process restart PASS.

Until those are recorded, N2 remains ACTIVE.
