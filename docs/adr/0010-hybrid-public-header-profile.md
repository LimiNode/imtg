# ADR 0010: hybrid public header profile

**Status:** Accepted for new work; implementation migration staged.
**Scope:** public C++ include and optional-target boundaries.
**Owner:** core architecture maintainers.
**Review trigger:** a new domain, independently usable component, or optional module.

## Context

ADR 0009 established domain umbrellas and an acyclic domain graph. The existing
Actions components are independently usable; commerce is optional, and MDBX,
payment providers, transports, and operator UI must not become core dependencies.
Requiring every consumer to include a broad umbrella would obscure these
boundaries. Directory nesting alone does not establish a dependency.

## Decision

Use a hybrid profile. Complete domains expose root-level umbrellas and keep
their owned leaves beside implementations. Public standalone components remain
self-contained entry points. Optional modules expose independently enabled
targets and documented headers; they are not aggregated by core umbrellas.
Provider-neutral contracts must not include provider implementations or SDKs.

Retain ADR 0009's DAG, deterministic umbrella order, local intra-domain includes,
public cross-domain entry points, and compatibility-preserving staged migration.
All currently installed component paths remain public; new private leaves are
excluded by an explicit install manifest. A source-only optional adapter is
not installed until its target and dependency-resolution contract are complete.

Test every supported umbrella and standalone header as a first include, test
core-only and optional configurations, and verify installed consumers. Declare
independent boundaries in the architecture inventory and header Doxygen. This
decision changes policy, not runtime behavior or the current install manifest.

## Consequences and alternatives

The public surface remains discoverable without making optional dependencies
transitive. Maintaining the inventory and package tests is a deliberate cost.
Existing nested umbrella paths require a declared compatibility-breaking release
before removal; this decision does not remove them.

A strict umbrella-only profile would impede independent optional use. Pure
leaf-only includes would lose convenient domain entry points. A speculative
plugin ABI, dynamic loader, per-type file split, or new target for every header
is unnecessary and is not part of this decision.
