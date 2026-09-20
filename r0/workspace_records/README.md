# Codynex R0.1 — Workspace Records Shadow Lab

R0.1 is the first real-system Codynex proof.

It shadows the real RiftOS Workspace Records change-interpretation workload while
leaving RiftOS production source and the live workspace untouched.

## Production baseline

Read-only baseline boundary:

- `RiftFileIdentityV2`
- `RiftDiffEngineV2`
- integration limits from `RiftWorkspaceRecords`

Exact source hashes are recorded in:

`BASELINE_PROVENANCE.md`

## Shadow rule

The Codynex path receives raw before/after evidence only:

- path;
- size;
- SHA-256;
- optional bounded text;
- declared resource limits.

It does **not** receive:

- baseline relation output;
- baseline diff output;
- expected labels;
- production scheduler/algorithm state;
- prior generated machinery after a destruction gate.

## Initial implementation order

1. corpus and semantic result schema;
2. exact identity evidence lane;
3. bounded heuristic lane;
4. bounded diff lane;
5. machinery destruction/reconstruction;
6. missing-evidence/fallback lane;
7. conventional baseline harness;
8. semantic comparator;
9. real replay corpus;
10. resource comparison.

## Planned layout

```text
r0/workspace_records/
  README.md
  VALIDATION.md
  BASELINE_PROVENANCE.md
  corpus/
    controlled/
    real/
  include/
    r0_types.h
    r0_limits.h
    r0_identity.h
    r0_diff.h
    r0_reconstruct.h
    r0_result.h
  src/
    r0_identity.cpp
    r0_diff.cpp
    r0_reconstruct.cpp
    r0_result.cpp
  host/
    CMakeLists.txt
    main.cpp
  evidence/
```

The layout is experimental test equipment, not permanent architecture.

## Non-mutation rule

R0.1 must perform:

- zero writes to `workspace/RiftOS-main`;
- zero writes to the live RiftOS workspace;
- zero writes to the live Workspace Records store.

Any real replay is read-only or generated inside a disposable test workspace.

## Protected trust split

Exact content identity:

- authoritative only from SHA-256 equality.

Heuristic relation:

- never promoted to exact;
- bounded;
- may abstain.

Diff:

- bounded;
- may truncate;
- must explicitly report incompleteness.

## Stage boundary

A green host test is not enough to complete R0.1.

R0.1 completion requires the full controlled corpus, real replay corpus,
destruction/reconstruction proof, resource accounting, baseline comparison and
anti-cheat audit defined in:

`docs/R0_WORKSPACE_RECORDS_REAL_SYSTEM_PROOF.md`
