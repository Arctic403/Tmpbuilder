# Codynex LR0 Live Runtime Lab

This tree contains the first Android LR0 implementation.

## Boundary

The native runtime library contains only generic CXE1/runtime mechanisms:

- bounded executable decode/validation;
- ProgramImage;
- transactional I64 execution;
- active/candidate replacement;
- explicit persistent-state export/restore;
- two-slot crash/restart recovery storage;
- JNI host boundary.

The Kotlin lab/editor is test equipment.

It creates an external `candidate.cxe` file from a tiny CXE1 assembly text and then asks the native runtime to load that file through the normal loader.

Program A/B behavior is not compiled into the native runtime.

## Android targets

- compileSdk 36
- targetSdk 36
- minSdk 26
- NDK 28.2.13676358
- CMake 3.22.1
- Java 17
- ARM64 primary
- ARM32 (`armeabi-v7a`) compatibility

## Host proofs

`host/codynex_lr0_host` checks:

- external Program A/B files;
- state-preserving +1 -> +5 replacement;
- 12 malformed candidate rejections;
- incompatible persistent schema rejection;
- checked I64 overflow rollback.

`host/codynex_lr0_recovery` checks:

- explicit persistent-state export;
- recovery into a fresh runtime object;
- continued execution after recovery;
- second restart;
- corruption of the newest journal slot;
- fallback to the previous committed slot.

## Device proof

The APK lab exposes:

- editable CXE1 assembly;
- compile to external `candidate.cxe`;
- activate existing external candidate;
- generic function invocation;
- generic state inspection;
- candidate-integrity corruption;
- runtime snapshot;
- explicit recovery reopen;
- real process kill for cold-restart proof.

A successful LR0 device proof requires the APK to remain unchanged while external hosted behavior changes.
