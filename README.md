# Tmpbuilder — Codynex N0 Temporary Builder

Disposable public CI surface for the Codynex N0 native Android substrate lab.

Canonical Codynex source remains outside this repository. This repo contains
only the N0 build surface required to compile and verify the current native lab.

## Purpose

The workflow proves:

- the C++ N0 host suite compiles with warnings-as-errors;
- the host-native experiment returns `"pass": true`;
- the Android project builds with the pinned AGP/Gradle/JDK/NDK/CMake toolchain;
- the APK contains both:
  - `arm64-v8a/libcodynex_n0.so`
  - `armeabi-v7a/libcodynex_n0.so`
- the packaged ELF files report the expected ARM64/ARM32 architectures;
- SHA-256 evidence is emitted for the APK, both native libraries and host result.

## Toolchain

- Android Gradle Plugin: 9.3.0
- Gradle: 9.5.0
- JDK: 17
- compileSdk / targetSdk: 36
- minSdk: 26
- Android Build Tools: 36.0.0
- Android NDK: 28.2.13676358
- CMake: 3.22.1
- C++: C++17 with `-Wall -Wextra -Wpedantic -Werror`

## ABIs

- `arm64-v8a`
- `armeabi-v7a`

## Run

Every push to `main` automatically triggers **Codynex N0 Temporary Builder**.

Manual `workflow_dispatch` is also retained as a fallback for reruns that do
not require a source change.

After a passing run, download the
`codynex-n0-<run-number>` artifact from GitHub Actions.

## Evidence bundle

A passing run uploads for 7 days:

- `codynex-n0-debug.apk`
- `libcodynex_n0-arm64-v8a.so`
- `libcodynex_n0-armeabi-v7a.so`
- `host-results.json`
- `elf-arm64.txt`
- `elf-arm32.txt`
- `apk-contents.txt`
- `SHA256SUMS.txt`

## Security / scope

This is disposable laboratory infrastructure.

The workflow has only:

```yaml
permissions:
  contents: read
```

No repository secrets are required for the debug build.
No Codynex signing key is stored here.

Do not treat this builder repo, GitHub Actions, Gradle, CMake, JNI or the debug
APK signing path as Codynex architecture. They are test/build equipment.

## Disposal

After N0 evidence has been captured and the roadmap no longer needs this public
builder, the workflow/repository may be archived or deleted.

Anything published to a public repository should still be treated as public even
after later deletion.
