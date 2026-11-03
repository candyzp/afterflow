# Agent A — GPU / Rendering Investigation

## Repository and branch

- Intended writable repository: `candyzp/afterflow`
- Upstream project checked: `afterflow-gd/afterflow`
- Reason for selecting the user repository: both repositories contain the same Afterflow project and matching core UI code, but the organization repository is read-only for this account. The user repository has write access and contains newer iOS-related commits.
- Working branch: `agent-a-gpu-rendering`
- Base branch: `main`
- Base commit SHA at investigation start: `c482db77d11044adaab6fda01a5430944c8b24a0`
- Source implementation commit: `a8d21aa58af8a2b239148bc741d83d8ff82c9a56`
- No merge or main-branch update was performed.

## Agent communication reviewed

The coordination folder and relevant agent communication paths were checked in both repositories.

- `coordination/agent-a-gpu-rendering.md`: absent before this report was created (404 on both repositories).
- `coordination/README.md` and `coordination/AGENTS.md`: absent (404).
- GitHub code searches for `coordination`, `agent-a-gpu-rendering`, `agent-b`, and `ownership`: no results in either repository.
- Pull requests in `candyzp/afterflow`: none found, open or closed.
- Issues in `candyzp/afterflow`: none found.
- Branches in `candyzp/afterflow`: `main`, `ios-build-test`, and this newly created Agent A branch.
- The `ios-build-test` branch was compared with `main`. It is diverged, with two commits ahead and six behind; its reported file changes are limited to `.github/workflows/ios-build-test.yml` and `CMakeLists.txt`. It does not modify `src/main.cpp`.

No messages from Agent B or other agents were published in the GitHub repository, so there were no agent messages to quote or acknowledge. I cannot access other ChatGPT conversations directly. No agent identity or message was inferred.

### Acknowledgments and responses

- Agent A: no prior published message was available to acknowledge. This report publishes the investigation and ownership findings for other agents.
- Agent B / other agents: no published message was available. Please publish branch, file ownership, findings, and proposed changes here before overlapping edits to `src/main.cpp`.

## Ownership and working-tree limitations

- No published branch or pull request was found that edits `src/main.cpp` concurrently.
- The existing `ios-build-test` branch's visible diff does not touch `src/main.cpp`.
- No ownership conflict was discovered in GitHub's published state.
- A local checkout and local working tree were not available in this environment. GitHub's remote branch state was inspected instead; uncommitted local changes cannot be observed or ruled out.

## Rendering investigation

The supplied approximate line numbers were checked against the current `candyzp/afterflow/main` version. The UI code is present but shifted by newer code in this repository.

### Findings

1. **`makeDrawNode()` and `drawCurvedRect()`**
   - `makeDrawNode()` creates a `CCDrawNode` and sets the blend function.
   - `drawCurvedRect()` generates rounded-rectangle polygon points on the CPU. It uses four arcs with 12 segments each, calculates trigonometric coordinates, filters duplicate points, and submits the polygon.
   - This is CPU geometry generation and draw-node update work; it is not, by itself, evidence of a GPU execution bottleneck.

2. **Panel glow construction**
   - The glow node is created and its five rings are drawn during deck construction.
   - The work is gated by the UI glow setting and is not continuously rebuilt by the animation loop.
   - This is not a per-frame bottleneck and was left unchanged.

3. **`AfterflowOverlay::tutorialFrame()`**
   - It is called from `animTick()` while the panel is open and a tutorial layer exists.
   - The spotlight's position, pulse, shade polygons, and outline change during the tutorial animation, so their per-frame geometry updates are expected.
   - The progress-pill draw node was cleared and rebuilt every tutorial frame, even after the interpolated progress value had stopped changing. Its pill widths and reached-state colors depend only on that interpolated value, so the geometry is static once the value settles.
   - This was selected for a small cache improvement.

4. **`AfterflowOverlay::drawTabIndicator()`**
   - The tab indicator is redrawn while its position is moving.
   - `animTick()` stops calling it once the interpolation settles. No change was justified.

5. **Hidden interface elements**
   - Tab changes toggle visibility for the corresponding pane elements.
   - No evidence was found that hidden pane elements are continuously regenerating draw geometry in the normal animation loop.

### Bottleneck assessment

A GPU hardware bottleneck was **not established**. No GPU timings, frame captures, or device-side profiling data were available. The source does show avoidable CPU-side geometry regeneration in the settled tutorial progress indicator. The change below removes that redundant work; it should not be interpreted as a measured GPU throughput improvement.

## Proposed and actual implementation

Implemented one focused change in `src/main.cpp`:

- Added `m_tutProgressNeedsRedraw` to track when the progress draw node needs its initial geometry.
- Set the flag when a new tutorial progress node is created for a step.
- In `tutorialFrame()`, preserve the existing progress interpolation, but clear and rebuild the pill geometry only when the interpolated value changes or the new node needs its first draw.
- Kept the original geometry formulas, pill widths, colors, ordering, alpha, and interpolation rate unchanged.

The original visual state is still computed from the same value. When that value is unchanged, drawing the same geometry again is unnecessary. No gameplay, prediction, input, orb, wave, stage, crash-fix, or iOS compatibility code was changed.

### Actual code changes

- File: `src/main.cpp`
- Member state: around line 3475
- Tutorial progress node creation: around line 5065
- Tutorial progress rendering: around line 5301
- Source commit: `a8d21aa58af8a2b239148bc741d83d8ff82c9a56`

## Build and test results

- Diff review: passed; the source commit changes only `src/main.cpp`.
- Scope review: passed; only tutorial progress rendering state and redraw condition changed.
- Build: not run.
- Runtime visual/performance test: not run.
- The local container could not clone GitHub because network name resolution was unavailable. A local Geode SDK/build environment was not available.
- The commit status endpoint reported no status checks. The available workflow-run query is limited to pull-request-triggered runs and returned none; it does not establish whether other event types ran. The GitHub integration available here did not expose a workflow-dispatch action, so no build was run. Compilation and runtime behavior remain unverified.

## Remaining questions for other agents

1. Does another agent have unpublished local changes or a local claim on `src/main.cpp`?
2. Is there a baseline performance trace for tutorial animation CPU time or frame timing on the target device?
3. Can UI theme colors be changed externally while the tutorial overlay remains open? If so, confirm whether the cached progress pills need a color-change invalidation signal.

Until those questions are answered, this branch remains isolated and must not be merged without authorization.
