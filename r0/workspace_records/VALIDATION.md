# R0.1 Validation Contract

R0.1 is a real-system shadow proof.

It does not pass because the shadow engine builds or because some outputs look
similar to RiftOS.

## Production immutability

Required:

- `workspace/RiftOS-main` remains read-only;
- live RiftOS workspace remains unmodified by R0.1;
- live Workspace Records store remains unmodified by R0.1.

Required mutation count caused by R0.1:

- 0.

## Baseline provenance

Before comparison, verify the production hashes recorded in
`BASELINE_PROVENANCE.md`.

If any hash changes:

- stop;
- re-audit baseline behavior;
- refresh provenance;
- do not compare against stale baseline evidence.

## Exact identity gate

Required:

- exact ground-truth precision: 100%;
- exact ground-truth recall: 100%;
- false exact relations: 0;
- heuristic relation marked exact: 0;
- deterministic semantic replay: 100%.

SHA-256 equality is the authoritative content-identity evidence in this R0.1
scope.

## Rename consumption gate

Required:

- one removed source consumed by at most one rename;
- one added target consumed by at most one rename;
- duplicate rename-consumption violations: 0.

## Bounded heuristic gate

Required:

- candidate files per side <= 64;
- similarity comparisons <= 1024;
- heuristic result remains non-exact;
- budget exhaustion is explicit;
- no silent all-pairs scan.

## Bounded diff gate

Required:

- exact matrix <= 250000 cells;
- recursion depth <= 64;
- rendered output <= 64000 characters;
- represented changed lines <= 420 before explicit truncation;
- incomplete output explicitly marked;
- non-truncated controlled diff coverage: 100%.

## Controlled corpus gate

Minimum required controlled cases:

1. unchanged;
2. exact rename;
3. exact copy;
4. edited rename;
5. competing rename candidates;
6. unrelated add/delete;
7. major rewrite;
8. tiny rewrite below rewrite threshold;
9. binary/metadata-only;
10. CRLF/LF;
11. duplicate lines;
12. >64 candidates per side;
13. 1024-comparison saturation;
14. exact-LCS diff;
15. patience-anchor diff;
16. ambiguous replacement fallback;
17. >420 changed lines;
18. >64000 rendered characters.

Hard cases may not be deleted after failures are observed.

## Destruction/reconstruction gate

Required:

- identity cases: 12/12;
- diff cases: 12/12;
- generated machinery retained bytes: 0;
- evidence mutation caused by reconstruction: 0;
- at least 50% of reconstruction cases regenerate materially different temporary
  machinery or execution representation.

## Missing-evidence gate

Required explicit fallback/uncertainty for:

- text unavailable;
- resource budget unavailable;
- similarity budget exhausted;
- diff text unavailable;
- ambiguous candidates.

Forbidden:

- manufacturing exact or heuristic certainty from erased evidence.

## Real replay gate

Minimum replay target:

- >=100 changed-file cases;
- >=10 exact rename/copy cases with known ground truth;
- >=10 substantial text modifications;
- >=5 large/ambiguous diff cases;
- >=5 binary/metadata-only cases.

If the available honest corpus cannot meet this target, classify the run
`LIMITED`; do not fabricate data and call it real replay.

Required:

- exact relation semantic parity with baseline: 100%;
- false exact relations: 0;
- deterministic Codynex replay: 100%;
- all bounds respected;
- all incomplete results explicit;
- RiftOS mutations: 0.

## Heuristic disagreements

Heuristic disagreement with baseline is measured rather than automatically
failed.

Every disagreement must be classified:

- Codynex error;
- baseline error;
- both plausible under incomplete evidence;
- ground truth unavailable.

The unresolved disagreement rate must be reported.

## Resource gate

Required:

- no unbounded growth;
- shadow runner peak core-data proxy <= 1 MiB;
- per-correlation similarity comparisons <=1024;
- no diff exact matrix >250000 cells.

Record but do not initially gate:

- runtime ratio versus baseline;
- generated index bytes;
- scratch bytes;
- reconstruction cost.

## Authority gate

The R0.1 shadow must not gain:

- Git authority;
- MCP authority;
- Local Agent authority;
- patch accept/deny authority;
- workspace-write authority.

Required authority expansion events:

- 0.

## Anti-cheat gate

Fail R0.1 if implementation:

- copies production control flow into a hidden generated recipe;
- queries baseline output during generation;
- persists expected answers;
- silently raises bounds;
- marks heuristic evidence exact;
- omits generation/reconstruction cost;
- requires live RiftOS mutation;
- requires permanent workload-specific generated machinery.

## Overall classification

Allowed:

- SUPPORTED
- LIMITED
- FALSIFIED
- INCONCLUSIVE
- BLOCKED

R0.1 is not complete until controlled + real replay + reconstruction + resource
+ baseline-comparison + audit gates all run.

A passing R0.1 is only the first real-domain proof.

R0 as a whole still requires at least one substantially different second real
domain.
