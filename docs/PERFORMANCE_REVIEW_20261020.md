# SDK performance review — 2026-10-20

This review changes twelve SDK modules and adds a reproducible declaration
inventory, regression coverage, installed-package probes and paired benchmarks.
The purpose is to remove repeated work from shared runtime, image, model,
metadata, document and synchronization paths while preserving their contracts.
No package version was changed for this review.

## Scope and evidence

The starting inventory contains 45 SDK module directories. The later snapshot
also contains the Windows build/verification toolkit; it is classified as
platform tooling, not a 46th runtime SDK. The declaration index covers owned
C/C++/Objective-C, QML, JavaScript/TypeScript, Svelte script blocks, Python,
Rust and Swift sources. Generated builds/package output and vendored code are
excluded; test and tool declarations are retained with a separate role.

Evidence is retained in `build/performance-20261020/`:

- `baseline-inventory.json`, `baseline-code.zip` and
  `baseline-code-manifest.json` pin the source before this work.
- `audit-baseline-v2/` and `audit-current-v2/` contain source hashes,
  object/member/function locations, callback/loop analysis, review leads and
  explicitly reported parser recovery locations.
- `paired-all-results.json` contains all raw samples, checksums, executables,
  platform information and medians for the before/after probes.
- Module build logs, JUnit reports, staging prefixes and separate installed
  consumers are retained beside the inventory.
- `final-report.json` records final validation status, exact coverage totals,
  retained failures, concurrent changes and evidence boundaries.

This is a source-wide syntactic inventory followed by contract review and
targeted implementation. It is not a claim that every declaration received a
complete manual semantic proof, every static review lead is a defect, or every
possible SDK bottleneck has been eliminated. C++ preprocessing, recovered
Objective-C/Qt syntax, Svelte templates and shaders need additional context.
Declaration counts can include header/implementation occurrences and callbacks.

The pinned starting snapshot indexes 1,617 runtime source files, 4,874 object
declarations, 17,633 implemented functions/callbacks and 24,758 member/method
declarations. The later snapshot indexes 1,669 runtime files, 5,027 objects,
18,158 implemented functions/callbacks and 25,620 member/method declarations.
Parser recovery is explicitly recorded for 126 starting files and 131 later
files. Review leads (1,267 initially; 1,287 later) are candidates for semantic
inspection, not a count of confirmed or unfixed performance bugs.

Separate ongoing work added or changed LVUI, iiGUI, iiNetwork, Society Helper
and LVRS graphics files during the review. Those edits were preserved and are
not attributed to this performance work. Later coverage counts therefore do
not measure the size of this refactor. The pinned current snapshot predates
some final small fixes; the final manifest records the task-owned final files.

## Implemented changes

| Module | Objects or methods | Change and preserved behavior |
| --- | --- | --- |
| iiSDKCore | `EventLoop` selection, deadlines, I/O refresh, utilization; UTF-8 validation | A dense private live-slot index removes repeated scans of unused capacity. Ordinary task retirement no longer recomputes real-time utilization. One normal-task selection pass retains FIFO, the 4:2:1 priority wheel, EDF ties, cancellation, token generations and capture retirement outside locks. ASCII runs use alignment-safe eight-byte reads; Unicode validation is unchanged. |
| iiColorManager | picker surface generation | Hue and brightness rails calculate one color per row, then copy pixels across that row. Scalar pixel parity covers every surface and edge dimension. |
| iiPaintEngine | blur in `applyFilterPipeline` | Column sums and a sliding horizontal window replace square-neighborhood work. The result uses the same clipped edges, selection predicate, ARGB channels and final integer division. Memory is bounded by image width plus the existing source copy. |
| iiGraphicAdjust | `boxAxis`, Gaussian/box/guided filters, color halftone | One private scratch image is reused through the existing passes. Three channel rotations are computed once per halftone invocation. Premultiplication, edge repetition, operation order and small-sigma accumulation remain intact. |
| iiLocalLLM | `ParameterCatalog` import, field lookup and nested validation | Field/required-field indexes and schema expressions are constructed once per immutable catalog. `anyOf` stops after acceptance while outer constraints still execute. Redaction continues visiting all branches. Concurrent const validation is tested. |
| LVRS | hierarchy visibility, table visible cells, text bulk read/save | Iterative ancestry memoization removes repeated parent walks and terminates cyclic branches. Per-call geometry and active merge spans replace repeated table coverage scans. Bulk text reads use one provider handle; unchanged saves avoid model reset. Missing/short source reads fail rather than publish incomplete output. Save-as observers see updated line storage. |
| iiCalendar | recurrence expansion | Month length is computed once per encountered calendar year/month/leap-month combination. Every candidate day still counts toward scan/COUNT limits, exclusions, DST and output bounds. |
| iiFileProvider | native authorship validation, object snapshot capture, creation audit | Immutable regular expressions are reused. Bounded descriptor-based POSIX copies avoid APFS clone-induced source-ctime changes and false revisions. Disposable snapshots omit a redundant fsync; durable final object transactions retain their existing synchronization. The source audit batches scanner reads without relaxing policy or unreadable-file rejection. |
| iiMultimodalAgent | shell permission inspection | Fixed descriptor/format/count expressions and allowed-flag tables are immutable and reused. Dynamic paths, glob confinement, parser work limits and per-request cancellation remain evaluated for each request. |
| iiGeneralDocument | PDF form field synchronization | A local object-number/generation index replaces repeated identity searches. Name fallback stays live so earlier field renames retain their original behavior. An independently assembled 256-field PDF verifies reverse-order edits and round trips. |
| iiHtmlBlock | HTML serialization | Sorted interval containment uses the furthest emitted end; sorted index lookup uses binary search. Partial overlaps, nested intervals, adjacent fragments and invalid indices retain their existing behavior. The build now requires standard C++23. |
| iiSocietySync | `Replica::bindHost` adoption plan | One prepared insertion is reused through the bounded detach plan in the existing transaction. Per-entry temporary lists and repeated SQL preparation are removed. Recovery order, durability, operation locking and resumable adoption are preserved. |

There are deliberate resource tradeoffs. Core reserves two additional `size_t`
values per capacity slot (1 MiB at 65,536 slots on this 64-bit host) in exchange
for live-slot traversal. GraphicAdjust removes repeated allocation and clearing,
but peak blur workspace remains two images. FileProvider copies snapshot bytes
instead of sharing APFS extents; correctness and stable source identity take
priority over clone-only storage savings. No unbounded global result cache was
introduced.

## Paired measurements

The probes use Release C++23 builds on this Apple Silicon macOS host. Original
source comes from the saved ZIP; the original Core library is built separately.
Each program receives one warm-up run followed by five alternating before/after
runs. Checksums must match on every sample. The JSON evidence is authoritative
for precise timings; the table below is generated from it.

| Probe | Before median (ms) | After median (ms) | Ratio before / after |
| --- | ---: | ---: | ---: |
| Idle deadlines, 1,024 capacity / 10,000 queries | 3.6476 | 0.2687 | 13.57× |
| Idle deadlines, 65,536 capacity / 10,000 queries | 733.6800 | 0.2688 | 2729.13× |
| Hue rail / repeated surface generation | 11.7008 | 0.8052 | 14.53× |
| Brightness rail / repeated surface generation | 11.7801 | 0.4815 | 24.47× |
| Channel ramp control (unchanged) | 0.2291 | 0.2373 | 0.97× |
| Sparse dispatch, 1,024 capacity / 3,000 tasks | 7.2430 | 0.5519 | 13.12× |
| Sparse dispatch, 65,536 capacity / 3,000 tasks | 1519.5000 | 0.5383 | 2823.04× |
| UTF-8 ASCII / 100 MiB | 33.7418 | 4.1644 | 8.10× |
| UTF-8 Korean/emoji / 100 MiB | 57.8825 | 48.4364 | 1.20× |
| Gaussian / 768×768 / radius 12 | 28.4623 | 27.8424 | 1.02× |
| Color halftone / 128×128 | 4.7302 | 3.4721 | 1.36× |
| ARGB blur / 384×384 / radius 1 | 2.4721 | 0.9543 | 2.59× |
| ARGB blur / 384×384 / radius 12 | 106.2190 | 0.8525 | 124.60× |
| Authorship JSON / 2,000 parses | 78.1811 | 70.7925 | 1.10× |
| Nested catalog / 512 fields × 400 validations | 1877.2600 | 205.6910 | 9.13× |
| Hierarchy chain / 5,000 nodes | 615.2050 | 1.9854 | 309.87× |
| Visible cells / 400 rows × 8 columns | 2530.4000 | 8.3987 | 301.28× |

These are method microbenchmarks, not application startup, FPS or process memory
measurements. The 65,536-slot case is intentionally capacity-sensitive and is
reported separately from the 1,024-slot case. Gaussian workspace reuse has a
small elapsed-time effect in this input; it must not be advertised as a large
speed improvement. Already efficient control paths are retained in the results,
including normal run-to-run variation.

## Other module dispositions

The index retains method-level review leads for unchanged modules. A loop,
full scan or lock is not removed solely because a static pattern flags it.
The following records distinguish required work and unproven candidates from
the twelve implemented changes.

| Modules | Review disposition |
| --- | --- |
| Flamewall, Silentlocked, iiComputerHistory, iiComputerUse, iiGlobalSearch, iiPBRMaterial | No owned runtime implementations in the starting inventory; no fabricated optimization work. |
| iiActorObject, iiWhatsNew | Minimal exported smoke implementations; no repeated data-processing hot path. |
| LVUI | No owned runtime implementation in the starting snapshot; a separate task added a parser during the review. |
| LVRS-Svelte | Script declarations are indexed; generated `.svelte-kit`/`dist` output is excluded. No browser-level performance gain is asserted without a consuming application profile. |
| iiNetwork | The inspected send pump already advances a byte offset in a deque instead of repeatedly erasing a buffer prefix. Separate native-network expansion is preserved. |
| iiGUI | Frame coalescing, deadlines and explicit surface capture have existing behavior. Separate text/renderer/backend work is preserved. |
| iiDeviceMultimedia, iiCameraIntegration | Device callbacks, bounded streaming and platform ownership require their existing I/O/lifetime contracts. No speculative scheduler or device cache was added. |
| iiDecision | Existing backend slots serialize non-reentrant contexts; independent work is not placed behind a new global lock. Evidence comparisons are input-dependent required work. |
| iiAudioGen | Process stop polling is a bounded graceful-shutdown path. Generation/transport contracts are preserved. |
| iiVoiceOver | Reusing Piper contexts without controlling process-global eSpeak state could change voice/language behavior. No speculative persistent-context cache was added; native speech remains covered by consuming-agent checks. |
| iiCSMIDI | Variable-length quantities are bounded to four bytes; short alphabet lookup and prefix insertion are bounded work. |
| iiSDKBuilder | Resource candidate enumeration is bounded by platform variants; canonical paths and confinement checks remain necessary. Core dispatch/text gains are available to consumers of the updated Core package. |
| iiPhotoLibrary | Existing source stamps and preview reuse prevent repeated content work. Discovering external library changes still requires a scan; preview reads already have a size bound. |
| iiSharedCanvas | Validation visits the mutable document graph. A persistent index cannot be made correct without a revision/invalidation contract; no speculative cache was introduced. |
| iiSocietyContainer | Storage locks, durable transactions, streamed payloads and maintenance are retained. Provider snapshot regressions are covered at their owning layer. |
| iiSocietyHelper | Filesystem identity and preview-version checks remain necessary. Separate media export work is preserved. |
| iiSocietyGeneration | Backend-context serialization and cancellation/epoch handling are retained. Generated outputs continue through FileProvider. |
| iiSocietyMonitor | Observation timestamps provide heartbeat/staleness information even if payload values are unchanged; changed sections are already compared. |
| iiWebServer | Authentication, peer ownership and transport lifetime remain existing contracts. No unrelated thread-dispatch architecture was added. |
| iiSocialPublish | Chunk bounds and remote-processing polling intervals are intentional. No shortened polling interval or unbounded batching was introduced. |
| iiLocalDiffusion | Existing resident-weight and preview gating paths are retained. Native matrix/kernel loops are not classified as redundant merely because they are nested. |
| iiAccountManager, iiLicenseManager, iiUpdateManager | Bounded configuration/identity/authentication validation is preserved; no unproven parse/result cache weakens account or update checks. |
| iiJson, iiXml | Parser/serializer traversal is required by input/output size. Inspected XML active-tag/name indexes and direct comment/CDATA skipping are already present. |

## Validation and delivery boundaries

Every changed module was built. Regression evidence covers output parity,
selection, ordering, invalid inputs, concurrency/cancellation, immutable
validation, source identity, interrupted/resumed packaging, model index
persistence, PDF round trips and installed package use. Exact pass/retry status
is recorded in the final JSON and the module JUnit files.

All twelve changed modules are staged under the review's `staging/` directory.
Benchmark consumers resolve their recorded package paths there; independent
Agent and SocietySync consumers exercise exported packages. Native probes have
no Qt dependency. Legacy modules keep their pre-existing Qt dependencies; this
performance review adds no Qt runtime requirement to native Core/filter/provider
targets. Tree-sitter/Python inventory tooling is development-only.

The full LVRS suite includes 2,873 original SVGs at four raster sizes. A test-only
resource embeds the exact unnormalized bytes instead of reading thousands of
individual files from external storage. The checks remain in place. Existing
example-count fixtures were updated to the actual 92-item catalog. The full
Workspace file-creation correctness gate uses a 600-second execution budget;
policy patterns and failure conditions are unchanged.

Initial failures are retained rather than overwritten: APFS snapshot source
identity changes, stale LVRS catalog counts, raw-file SVG test I/O, a malformed
CTest selection expression, concurrent installed-LVRS relocation and native
inference/synchronization timing failures. Final status distinguishes repaired
failures, successful retries and any unresolved validation.

The selected source/module checks finish with **594 of 595 passing**, after
combining unique test names and successful targeted retries. This includes 100
selected Agent checks; 31 optional model checks from the 131-check registration
were outside the selected run. Repeated checks are not added to the denominator.
Standalone FileProvider Native checks also pass, as do eight development-tool
unit tests, six installed benchmark consumers, ten installed Agent checks and
eight installed SocietySync checks. Existing module suites additionally include
their own installed-header/export/consumer checks.

| Module | Selected checks passing / executed |
| --- | ---: |
| iiSDKCore | 43 / 43 |
| iiColorManager | 6 / 6 |
| iiPaintEngine | 49 / 49 |
| iiGraphicAdjust | 202 / 202 |
| iiLocalLLM | 20 / 21 |
| LVRS | 61 / 61 |
| iiCalendar | 15 / 15 |
| iiFileProvider | 39 / 39 |
| iiMultimodalAgent | 100 / 100 |
| iiGeneralDocument | 34 / 34 |
| iiHtmlBlock | 13 / 13 |
| iiSocietySync | 12 / 12 |

The remaining failure is `iiLocalLLM.mlx_inference`: the private MLX worker
exceeds its existing 120-second load deadline. It also fails with the original
pre-review library and unmodified worker (149.50 seconds overall). A separate
instrumented original-library attempt passed in 94.48 seconds. Instrumented
current-library traces show Python module reads, package-metadata reads and
Transformers import-structure traversal during the delay. This is unresolved
validation; it is not a successful GPU regression result. Native llama.cpp GPU,
llama.cpp CPU and MLX CPU checks passed in the full LLM run. No inference code,
worker deadline or GPU test limit was changed to obtain a pass.

Synchronization and shell-policy timing failures passed subsequent focused
checks with their internal limits intact. The shell diagnostic is isolated from
the shipped code; a fresh production permission object is byte-identical to the
original built object. Relinking and rerunning the normal source checks yields
passing memory-extraction, permission and runtime-binding checks. Installed
synchronizer/namespace/on-demand suite budgets now match their existing source
counterparts at 600 seconds, retaining all per-operation latency assertions.

Global `~/.local` packages were not replaced by this task, and running Product
applications were not restarted. No whole-application performance claim,
Windows/Linux runtime claim, commit or push is implied by local build/staging
evidence. The source, tests, documentation, native probes and evidence are ready
for review and subsequent application-level profiling.
