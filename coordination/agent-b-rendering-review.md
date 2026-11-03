# Agent B — Independent Afterflow Rendering Review

**Inspection date:** 2026-11-03  
**Reviewer:** Agent B  
**Final status:** APPROVED WITH CONCERNS (code-level review only; not merge authorization)

## 1. Repository status and identity

### Named repository in the task

- Repository requested by the user: `afterflow-gd/afterflow`
- Verified identity: its README identifies the project as Afterflow by prsevend, a Geometry Dash 2.2 / Geode mod. The requested rendering functions exist in its `src/main.cpp`.
- Branch inspected: `main`
- Main commit at inspection: `b19aec89233dcb8aed0331a4df79bed6d622a577`
- `src/main.cpp` blob inspected: `e892cf93065de7af1666e0690f64aed167aaa506`
- `CMakeLists.txt` blob inspected: `7b0ef4f7633efcbbb330e3e3afba703f8d0bd41f`
- The connected GitHub integration reports pull access but no push access for this repository. Agent B therefore cannot publish the required review file there.
- No source or build-file edits were made to this repository.

### Writable collaboration repository selected after discovery

The user originally supplied `candyzp/afterflow`. It is a distinct repository, not an alias for the organization repository. It contains newer user-specific iOS commits and Agent A's actual rendering branch. Its core rendering implementation was compared with the named repository: `makeDrawNode()`, `drawCurvedRect()`, `drawTabIndicator()`, and `animTick()` match after whitespace normalization. The tutorial code and surrounding project have diverged with newer fork changes.

- Collaboration repository: `candyzp/afterflow`
- Review base branch: `main`
- Review base commit: `c482db77d11044adaab6fda01a5430944c8b24a0`
- Agent A working branch: `agent-a-gpu-rendering`
- Agent A implementation commit under review: `a8d21aa58af8a2b239148bc741d83d8ff82c9a56`
- Agent B isolated publication branch: `agent-b-rendering-review`, created from the `candyzp/afterflow` `main` branch.
- Agent B's only intended writable path: `coordination/agent-b-rendering-review.md`

This is an explicit, documented switch to the user's original, writable repository for coordination. It is not a claim that the two repositories are identical, and it does not change the requested upstream repository.

## 2. Agent communications actually read

### Read in full

1. `coordination/agent-a-gpu-rendering.md` on `candyzp/afterflow`, branch `agent-a-gpu-rendering`, blob SHA `6803b8661f60c73a6ec3b77ce88e200ed688cf31`.
   - This is Agent A's full investigation report, including repository selection, ownership, source findings, patch description, test limitations, and questions.
2. Commit `a8d21aa58af8a2b239148bc741d83d8ff82c9a56`, titled `perf(ui): avoid rebuilding settled tutorial progress geometry`.
   - Read the complete commit diff. It modifies only `src/main.cpp`.
3. Agent A's current branch diff relative to its stated base `c482db77d11044adaab6fda01a5430944c8b24a0`.
   - The branch is three commits ahead of that base; the reported changed paths are the Agent A report and `src/main.cpp`.
   - Comparing from the implementation commit to the branch shows no later source-file changes; only the communication report was added afterward.

### Missing messages / replies

- The required coordination file was initially absent from both repositories' `main` branches. It was subsequently found on Agent A's dedicated branch as described above.
- The following likely companion paths were checked on Agent A's branch and were absent: `coordination/agent-b-rendering-review.md`, `coordination/agent-a.md`, `coordination/agent-b.md`, `coordination/shared.md`, `coordination/README.md`, and `coordination/status.md`.
- No Agent B message or shared coordination thread existed to read.
- Agent A's report says it had received no Agent B or other agent messages. That is consistent with the published repository state.
- No Agent A response to this review exists yet. Agent B has not received an acknowledgment; none is claimed.

### Acknowledgment and request

I acknowledge Agent A's narrow progress-geometry optimization and agree that the source patch removes redundant CPU-side draw-node rebuilding after the tutorial progress value settles.

I disagree with Agent A's statement in **“Rendering investigation → Hidden interface elements”** that no hidden pane elements continuously regenerate draw geometry. The current source contradicts that statement. Agent A is asked to review the evidence in Section 3, correct or qualify that finding in its own report, and reply in this file with whether it agrees and whether it intends to investigate the additional redraw sites. This is a concrete request, not an assertion that Agent A has replied.

## 3. Independent investigation

Line numbers below refer to `candyzp/afterflow` branch `agent-a-gpu-rendering`, the source snapshot containing Agent A's patch.

### Rendering architecture and CPU/GPU distinction

- `makeDrawNode()` (around line 2550) creates a `CCDrawNode` and sets `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`. Its blend behavior is intentional: the adjacent comment explains that the node's shader emits straight alpha. Changing that blend function as a speculative optimization risks translucent-fill visual regressions.
- `drawCurvedRect()` (around line 2571) builds rounded-rectangle polygon vertices on the CPU. It computes four 12-segment arcs with `std::cos`/`std::sin`, filters near-duplicate vertices, and submits the resulting polygon. Its point arrays are stack arrays. This demonstrates CPU geometry construction; it does not prove GPU saturation or establish that `CCDrawNode::drawPolygon` internally allocates/frees memory.
- The code contains no GPU timer, GPU frame capture, draw-call counter, or other device-side rendering instrumentation in the inspected paths. Existing `steady_clock` use is for other timing/budget logic, not GPU timing.
- `CCDrawNode::clear()` followed by draw calls is scene-graph/draw-data mutation on the CPU side. The downstream cost of buffer uploads, batching, rasterization, overdraw, or GPU execution was not measured. Do not label these operations a GPU bottleneck without device evidence.

### Named functions

**`AfterflowOverlay::tutorialFrame()`** (around line 5147)

- It returns immediately unless the tutorial layer and required nodes exist. The panel schedules `animTick()` while open and unschedules it on close and exit; the tutorial layer is removed through `clearTutorialLayer()` after the closing animation.
- The tutorial shade is cleared and rebuilt every frame. Its spotlight rectangle is eased over time and its outline pulses, so much of that geometry is genuinely animated. Caching the shade wholesale would freeze changing visuals or require a more careful cache/invalidation design.
- The ripple node is also cleared each frame, although it only draws circles while a ripple is active. This may be a separate small candidate for later measurement, but no change is recommended without checking how the node should behave when the ripple expires.
- The progress pills depend on the interpolated progress value. Their geometry does not need to be resubmitted once that value stops changing; Agent A's change addresses this specific redundant work.

**`AfterflowOverlay::drawTabIndicator()`** (around line 5328)

- It clears and redraws one rounded rectangle.
- Its animation caller invokes it while the indicator position is moving; when the indicator settles, the interpolation condition stops calling it. The tab-selection path also draws the initial/final state. This is already demand-driven enough to be a low-priority target.

**`AfterflowOverlay::animTick()`** (around line 5349)

- It returns when the panel is closed. The callback is scheduled when opening the panel and unscheduled on close and exit.
- It only redraws the tab indicator while the tab indicator is moving.
- It calls `refresh()` each animation tick when stage bars are moving on the Stage tab. That behavior is relevant to the hidden log-highlight finding below.

### Additional, independently verified hidden-pane redraw issue

Agent A's hidden-interface conclusion is not correct for the current branch:

- `applyTab()` makes `m_stageTrack` visible only for the Stage tab (around line 4148) and `m_logHighlight` visible only for the Log tab (around line 4135).
- However, `refresh()` unconditionally calls `m_stageTrack->clear()` and reconstructs the stage track inside its `if (m_stageTrack)` block (around line 4231). This work is not guarded by `m_activeTab == 1`.
- The same `refresh()` unconditionally calls `m_logHighlight->clear()` and rebuilds its highlight (around line 4488). This work is not guarded by `m_activeTab == 0`.
- `openPanel()` schedules `tickRefresh()` every 0.2 seconds (around line 4605). `tickRefresh()` calls `refresh()` whenever the panel is open, regardless of active tab.
- While the Stage tab's bars move, `animTick()` calls `refresh()` on the animation cadence (around line 5377). During that period, the hidden log-highlight node is also cleared/rebuilt each time.

Therefore, hidden nodes do continue to receive CPU-side geometry updates. This is source evidence of avoidable work, not a measured frame-time improvement. A safer follow-up would preserve model/text refresh behavior while gating only the draw-data rebuilds for the inactive panes, with a redraw/invalidation on tab activation. Do not early-return from all of `refresh()`: non-geometry labels and values still need their existing update behavior. Verify that every tab activation reaches a refresh before making a formerly hidden pane visible.

### CMake / platform configuration

The current Agent A branch `CMakeLists.txt` retains C++23 and the Apple architecture selection (iOS arm64; macOS arm64 and x86_64). It also contains the existing iOS GeodeBindings forced-include workaround for `Geode/fmod/fmod.hpp`. Agent A's implementation commit does not modify `CMakeLists.txt` or any platform configuration. This supports that the patch did not introduce a build-configuration change; it does not establish that the modified code compiles or that iOS compatibility has been tested.

## 4. Agent A implementation review

### Proposed change and exact implementation

Agent A proposed to stop rebuilding the tutorial progress-pill geometry after `m_tutProgShown` stops changing.

The implementation commit:
- adds `m_tutProgressNeedsRedraw` (around line 3477);
- sets it when a new progress draw node is created (around line 5068);
- stores the previous interpolated progress value, updates the value with the original easing expression, and clears/rebuilds the progress node only if the flag is set or the value changed (around lines 5306–5327).

The diff is limited to `src/main.cpp` (+21/-13 in the compare view). The implementation matches the stated proposal. It does not change input handling, prediction logic, player hooks, orb timing, wave behavior, stage settings, or CMake configuration.

### Technical assessment

**Code-level approval:**

- The original easing formula and target are unchanged.
- The same `nearness`, width, reached-state, colors, draw order, and coordinates are used whenever a redraw is needed.
- The first draw of each newly created progress node is forced by the flag.
- When the interpolated float stops changing, the already-submitted progress geometry remains on its node instead of being needlessly cleared/rebuilt.
- Tutorial-step construction creates a new progress node and sets the flag again, so a following step receives fresh geometry.
- `clearTutorialLayer()` removes the tutorial layer with cleanup and nulls the progress-node pointer. The patch introduces no additional scene node or separate owned allocation.
- The optimization retains the progress geometry on the existing node; it does not remove the geometry from the render path, reduce overdraw, reduce draw calls, or prove lower GPU execution time.

The exact float comparison is appropriate for preserving the prior value-by-value rendering behavior: the patch redraws whenever the value changes at all, and stops only when the float reaches a value that no longer changes under the existing easing update.

### Concerns and regression risks

1. **Color invalidation:** progress-pill colors are evaluated with `accentColor()` when geometry is rebuilt. If the accent color can change from an external settings path while the tutorial layer remains alive and `m_tutProgShown` is stable, the cached geometry would retain the old tint. The built-in tutorial interaction appears to restrict ordinary tab/theme interaction during an active tutorial, but external settings mutation and the tutorial-closing interval were not runtime-tested. Please confirm the intended invalidation path or explicitly state why theme changes cannot occur while this node is alive.
2. **No runtime comparison:** no rendered before/after capture was available to prove that the pills remain visually identical across step transitions, initial construction, and tutorial close.
3. **No performance measurement:** no timing data establishes the size of the CPU saving. It is fair to say repeated geometry construction is eliminated after settling; it is not fair to claim a quantified frame-time or GPU improvement.
4. **Incomplete audit scope:** the hidden `m_stageTrack` and `m_logHighlight` rebuilds above are material additional findings. They do not invalidate the progress-pill patch, but Agent A's written conclusion needs correction.

## 5. Verification record

- **Repository identity:** verified from repository search results and project README.
- **Source / CMake inspection:** completed remotely through GitHub file retrieval.
- **Agent A report and implementation diff:** read.
- **Source scope:** implementation commit changes only `src/main.cpp`; Agent B made no source edits.
- **Build:** NOT RUN by Agent B.
- **CI for Agent A implementation commit:** the commit-status and associated workflow-run queries returned empty lists. No successful build can be claimed.
- **Runtime:** NOT RUN.
- **Visual correctness:** not empirically tested. Code-level geometry math and lifecycle review found no direct regression in the narrow cache change, subject to the invalidation concern above.
- **Performance evidence:** no GPU timings, frame captures, or before/after benchmark. Only source-level CPU work elimination is established.
- **iOS compatibility:** not verified by Agent B. Agent A did not change CMake/platform settings, but this is not an iOS build test.
- **Uncommitted local changes:** cannot be inspected through the remote GitHub source view; the published branch state is what was reviewed.

## 6. Final recommendation

**APPROVED WITH CONCERNS**

The tutorial progress-pill cache is a justified, minimal CPU-side optimization and matches Agent A's stated implementation. Approving this code-level change does **not** establish a GPU bottleneck, does **not** certify a performance gain, and is **not** authorization to merge the branch.

Before treating the overall rendering audit as complete:
1. Agent A should correct its “hidden interface elements” finding using the `refresh()` evidence above and respond to the specific disagreement in this file.
2. Agent A should confirm the progress-pill cache's theme/color invalidation behavior.
3. A build and runtime visual check should be performed on the intended targets, followed by an instrumented before/after measurement if a performance claim is desired.

**Agent B ownership statement:** this report is the only path Agent B intends to create or update. Agent B did not edit `src/main.cpp`, `CMakeLists.txt`, Agent A's communication file, or Agent A's branch, and did not merge or update `main`.
