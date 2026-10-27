# Coding-agent workflow

## Before editing

Read `AGENTS.md`, the owning contract, relevant invariants, and any applicable subdirectory instructions. Run `git status --short`; preserve unrelated working-tree changes and do not reset or overwrite them. Identify whether the change is documentation, public API, implementation, dependency, or build work before choosing checks.

Create every local build, sanitizer tree, install staging area, generated fixture, and consumer probe
under `build/<profile>/`. Never configure in-source or add a root-level `build-*`,
`cmake-build-*`, coverage, or test-output directory. Inspect generated directories before any
cleanup and never use unrestricted `git clean -fdx`.

## While editing

Keep the diff focused and avoid drive-by refactors. A public API change updates its owning contract, Doxygen semantics, implementation status, and tests in the same change. New dependencies require an explicit boundary/ownership review and failure model. Do not add a dependency or submodule merely to make a conceptual example compile. Significant choices belong in an ADR; open questions must remain visibly open.

## Verification and review evidence

Run the smallest relevant checks and report exact commands and results. Documentation changes should check relative links, conflicting terminology/status claims, conceptual-example markers, and `git diff --check`. Implementation changes add focused unit/conformance tests, sanitizer or race checks where relevant, and a review of affected invariants. Do not run or claim an irrelevant C++ build when no target/build system exists. Never imply CI ran without a recorded workflow result.

Before requesting review, inspect the complete diff and `git status --short`, confirm the branch/base, summarize risks and open questions, and state what was not checked. Keep the PR in Draft when requested; never merge without explicit user instruction.

For a build-affecting change, use clean `build/debug` and `build/release` trees, install into
`build/install-stage`, and configure an independent consumer in `build/consumer`. Public-header
probes and sanitizer output also stay below `build/`.

## Completion

Use a Conventional Commit with a descriptive body when the change has meaningful trade-offs (for example, `docs: establish imtg architecture and engineering foundation`). The handoff reports changed files, exact HEAD, checks, CI status if available, remaining risks, and the recommended next step. Preserve any user changes that are outside the task.
