# imtg agent guide

This repository is documentation-first until the roadmap explicitly authorizes implementation. Read the relevant contract, architecture document, and this guide before changing files.

## Workflow

1. Run `git status --short` before work and preserve unrelated changes.
2. Keep changes scoped; do not perform opportunistic refactors.
3. Update the owning contract, implementation status, and tests when a public API changes.
4. Check architectural invariants and record non-obvious decisions in an ADR.
5. Do not claim checks, builds, integrations, or CI results that were not actually run.
6. Use Conventional Commits. Do not merge a PR without explicit user instruction.
7. Document every new public type and nontrivial public operation with semantic English
   Doxygen in the same PR as its implementation.

## Naming

- Classes, structs, enums, and type aliases use `CamelCase`.
- All methods and free functions use `snake_case`, including public Immediate-Mode UI methods (`ui.button()`, `ui.input_text()`).
- Private and protected fields use the `m_` prefix.
- Enum values use `CamelCase`.
- Macros use the `IMTG_` prefix.
- Namespaces use lowercase `snake_case`.
- Dear ImGui conventions influence the UI programming model, but do not determine imtg naming conventions.
- Exceptions require explicit architectural justification.

Detailed rules are in [coding-style](docs/guides/coding-style.md), [agent workflow](docs/guides/coding-agent-workflow.md), and [documentation governance](docs/DOCUMENTATION_GOVERNANCE.md). Milestone-specific scope belongs in [ROADMAP.md](docs/ROADMAP.md), not in this root router.
Public API documentation requirements and validation commands are in
[public API documentation](docs/guides/public-api-documentation.md).

## Engineering communication

The user has opted into the installed `sovetwave` skill for technical work on
imtg and direct follow-up discussions. Read its `SKILL.md` when available;
the repository's contracts and workflow remain authoritative. The evidence
and language boundaries are recorded in the
[agent workflow](docs/guides/coding-agent-workflow.md#engineering-explanation-and-evidence).
The skill is agent tooling, not a product or build dependency.

## Header architecture

- Complete domains expose a root-level umbrella at `src/imtg/<domain>.hpp`;
  domain leaves remain under `src/imtg/<domain>/`.
- Domain dependencies form a DAG. Umbrellas include lower-level public entry
  points and their own components in a deterministic order; leaves never
  include their own umbrella.
- Public standalone headers are self-contained. Private leaves may rely on a
  documented umbrella context, but are not installed or tested as public API.
- Cross-domain includes use `<imtg/...>` entry points; intra-domain includes
  use local paths; `..` relative includes are forbidden.
- The install manifest names supported public entry points explicitly. Update
  the topology audit and header tests when a domain or public boundary changes.

The current inventory, target graph, and migration policy are normative in
[header architecture](docs/guides/header-architecture.md) and
[ADR 0009](docs/adr/0009-domain-header-architecture.md).

## Repository structure

- Non-template `.cpp` implementations live beside their owning `.hpp` under `src/imtg/<domain>/`.
  Public include paths remain `imtg/...`; only public headers are installed.
- All CMake build trees, tests, generated files, temporary install stages, and local probes live
  under `build/<profile>/`. Do not create in-source or root-level ad-hoc build directories.
- Git-based third-party dependencies are pinned direct submodules under `external/<name>` by
  default. Prefer flat reuse and non-recursive initialization; document any exception explicitly.
