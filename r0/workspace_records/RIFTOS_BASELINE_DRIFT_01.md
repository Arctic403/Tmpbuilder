# R0.1 RiftOS Baseline Drift 01

R0.1 does not own or modify the RiftOS repository.

A read-only status check after the first R0.1 exact-identity implementation step
showed that RiftOS changed independently from the state recorded in
`RIFTOS_START_STATE.md`.

## Repository

- repository: Arctic403/RiftOS
- branch: main
- HEAD: 6f3d90a7c2b6e9cf9ee859afa947ce61bea3ff03

## Initial modified paths at R0.1 start

- scripts/test-semnexis-bootstrap.mjs
- scripts/test-semnexis-shell.mjs
- src/semnexis-bootstrap.js

## Later observed modified paths

- scripts/test-semnexis-arm32-exec.mjs
- scripts/test-semnexis-bootstrap.mjs
- scripts/test-semnexis-shell.mjs
- src/semnexis-bootstrap.js

## Drift

New externally modified path observed:

- scripts/test-semnexis-arm32-exec.mjs

R0.1 did not write this path.

All R0.1 writes up to this checkpoint were confined to:

`workspace/Codynex/r0/workspace_records/`

## Production baseline source status

The three R0.1 production baseline files remain identified by the hashes in
`BASELINE_PROVENANCE.md`.

No R0.1 write was made to:

- RiftFileIdentityV2.kt
- RiftDiffEngineV2.kt
- RiftWorkspaceRecords.kt

## Rule going forward

R0.1 may continue shadow implementation work because the observed drift is
outside the selected Workspace Records baseline boundary.

Before any real baseline comparison run, re-check:

1. RiftOS HEAD;
2. hashes of the three production baseline files;
3. current modified/deleted/untracked set.

If one of the selected baseline files changes, stop and refresh baseline
provenance before comparing outputs.
