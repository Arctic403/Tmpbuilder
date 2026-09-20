# Tmpbuilder — Codynex N2 Temporary Builder

Disposable public CI surface for the Codynex N2 self-reconstruction experiment.

Every push to `main` automatically runs the N2 host/build pipeline.

## What CI proves

A green run proves:

- C++17 host compile under warnings-as-errors;
- host/in-process N2 suite returns `"pass": true`;
- host report still returns `"stageComplete": false`;
- Android APK assembles;
- ARM64 native library packages and verifies as AArch64;
- ARM32 native library packages and verifies as ELF32 ARM;
- SHA-256 evidence is emitted.

## What CI does NOT prove

Green CI does not complete N2.

N2 still requires on the real Android device:

1. in-process N2 suite PASS;
2. Graph -> Tape cold restart PASS after real process termination;
3. Tape -> Graph cold restart PASS after real process termination.

## Toolchain

- AGP 9.3.0
- Gradle 9.5.0
- JDK 17
- compileSdk / targetSdk 36
- minSdk 26
- Build Tools 36.0.0
- NDK 28.2.13676358
- CMake 3.22.1
- C++17
- `-Wall -Wextra -Wpedantic -Werror`

## ABIs

- `arm64-v8a`
- `armeabi-v7a`

## Evidence artifact

A passing run uploads:

- `codynex-n2-debug.apk`
- `libcodynex_n2-arm64-v8a.so`
- `libcodynex_n2-armeabi-v7a.so`
- `host-results.json`
- ELF reports
- APK contents
- `SHA256SUMS.txt`

Retention: 7 days.

## Security / scope

Workflow permission is read-only:

```yaml
permissions:
  contents: read
```

No signing secrets are required for this disposable debug proof.

This repository is build/test infrastructure, not Codynex architecture.
