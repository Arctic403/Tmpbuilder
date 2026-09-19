# Tmpbuilder — Codynex N1 Temporary Builder

Disposable public CI surface for the Codynex N1 generated-machinery native
Android experiment.

Canonical Codynex source remains in the private/main Codynex workspace. This
repository contains only the N1 build surface required to compile and verify the
current native experiment.

## Purpose

Every push to `main` automatically runs the N1 proof pipeline.

The workflow verifies:

- C++17 host compile with warnings-as-errors;
- full host-native N1 suite returns `"pass": true`;
- dense and frontier machinery satisfy the N1 host gates;
- Android APK builds;
- both required Android ABIs are packaged;
- ELF architecture checks pass;
- APK/native-library/host-result SHA-256 evidence is generated.

## N1 experiment

Generated machinery under test:

- dense full-sweep execution;
- bounded generated frontier queue/index;
- temporary actionable-workload profile;
- automatic dense/frontier selection.

Required behavioral tests include:

- dense N0 parity;
- frontier N0 parity;
- dense -> frontier hot replacement;
- frontier -> dense hot replacement;
- frontier destroy/rebuild;
- sparse specialization evaluation reduction;
- broad distributed-corruption tradeoff;
- authority isolation;
- deterministic replay;
- controller-loss comparison;
- low-memory gate.

## Toolchain

- Android Gradle Plugin: 9.3.0
- Gradle: 9.5.0
- JDK: 17
- compileSdk / targetSdk: 36
- minSdk: 26
- Android Build Tools: 36.0.0
- Android NDK: 28.2.13676358
- CMake: 3.22.1
- C++17
- `-Wall -Wextra -Wpedantic -Werror`

## ABIs

- `arm64-v8a`
- `armeabi-v7a`

## Workflow

Automatic trigger:

```text
push -> main
```

Manual `workflow_dispatch` remains available for a rerun.

A passing run uploads:

- `codynex-n1-debug.apk`
- `libcodynex_n1-arm64-v8a.so`
- `libcodynex_n1-armeabi-v7a.so`
- `host-results.json`
- `elf-arm64.txt`
- `elf-arm32.txt`
- `apk-contents.txt`
- `SHA256SUMS.txt`

Artifact retention: 7 days.

## Security / scope

Workflow permissions:

```yaml
permissions:
  contents: read
```

No signing secrets are required for the debug proof build.

This public repo is disposable build/test infrastructure. It is not Codynex
architecture.

## N1 completion boundary

A green GitHub Actions run proves host-native + cross-ABI Android build/package
gates.

It does **not** finish N1.

N1 completes only after the generated APK runs the JNI/native suite on the real
Android device and reports:

```json
{"pass":true}
```
