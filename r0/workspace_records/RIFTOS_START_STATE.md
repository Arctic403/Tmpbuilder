# R0.1 RiftOS Starting State

R0.1 began against an already-dirty RiftOS working tree.

This dirt predates R0.1 and is outside the Workspace Records experiment.

## Repository state at R0.1 start

Repository:

`Arctic403/RiftOS`

Branch:

`main`

HEAD:

`6f3d90a7c2b6e9cf9ee859afa947ce61bea3ff03`

Pre-existing modified files:

- `scripts/test-semnexis-bootstrap.mjs`
- `scripts/test-semnexis-shell.mjs`
- `src/semnexis-bootstrap.js`

Pre-existing deleted files:

- none

Pre-existing untracked files:

- none

## R0.1 immutability rule

R0.1 must not add any new RiftOS modified/deleted/untracked paths.

The three pre-existing Semnexis modifications above are tolerated as baseline
state because they existed before R0.1 started.

R0.1 passes the non-mutation gate only if a later RiftOS status check shows:

- the same HEAD unless changed independently by the user;
- no R0.1-created production edits;
- no additional modified/deleted/untracked paths attributable to R0.1.

If RiftOS changes independently during the experiment, R0.1 must record the new
external baseline state before continuing comparisons.
