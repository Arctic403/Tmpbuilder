# R0.1 Baseline Provenance

## Scope

R0.1 compares the Codynex shadow implementation against the real production
RiftOS Workspace Records change-interpretation boundary.

RiftOS production source is **read-only** for this experiment.

No production file is copied into the Codynex shadow implementation as
algorithmic machinery.

## Production source hashes

### RiftFileIdentityV2.kt

Path:

`workspace/RiftOS-main/android/app/src/main/java/com/riftos/app/RiftFileIdentityV2.kt`

SHA-256:

`ec7848eb947897d221e36ebf15ce9f4a4fcf27fbe28d158f1e5a1c572d15ae8a`

Role:

- exact SHA-256 rename/copy evidence;
- bounded heuristic rename/rewrite correlation;
- deterministic relation ordering;
- similarity resource accounting.

### RiftDiffEngineV2.kt

Path:

`workspace/RiftOS-main/android/app/src/main/java/com/riftos/app/RiftDiffEngineV2.kt`

SHA-256:

`fd995af109de741a6942f20066c7932f1520e2b924faeb5c545f479c4a3d76ce`

Role:

- bounded adaptive text diff;
- exact LCS for bounded regions;
- patience-style anchors;
- bounded replacement fallback;
- explicit output truncation.

### RiftWorkspaceRecords.kt

Path:

`workspace/RiftOS-main/android/app/src/main/java/com/riftos/app/RiftWorkspaceRecords.kt`

SHA-256:

`57273a46311da8e6505eb99ba0943c296b658b38e3bbe8d7ca1dfb3219543b39`

Role:

- real production integration boundary;
- supplies before/after evidence;
- calls identity correlation;
- calls bounded diff rendering;
- current integration limits.

## Baseline limits captured from production source

Identity:

- minimum heuristic rename similarity: 60
- major rewrite maximum similarity: 25
- minimum major rewrite bytes: 512
- maximum similarity candidates per side: 64
- maximum line-similarity comparisons: 1024

Diff:

- exact matrix maximum: 250000 cells
- maximum recursion depth: 64
- context lines: 3

Workspace Records integration:

- maximum stored text bytes per file: 1048576
- maximum diff characters: 64000
- maximum represented changed lines: 420

## Trust boundary

Production baseline output may be used only **after** Codynex generates its own
result.

Forbidden:

- using baseline output as generator input;
- translating production control flow into a hidden Codynex recipe;
- changing production source to make the comparison easier;
- treating heuristic production output as authoritative ground truth.

## Refresh rule

If any of the three production hashes changes, this provenance record is stale.

Before another R0.1 comparison run:

1. re-read the changed production source;
2. record the new hash;
3. review semantic/bound changes;
4. update the R0.1 specification if required;
5. regenerate baseline evidence before comparing Codynex results.
