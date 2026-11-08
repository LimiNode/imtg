# ADR 0009: hierarchical domain header architecture

**Status:** Accepted for new work; migration staged. **Scope:** public C++
headers under `src/imtg`.

## Context

`imtg` already has several domains, but only some have umbrellas and those
umbrellas live below their domain directories. The recursive install currently
makes every header public, so consumers can accidentally depend on physical
layout or on transitive include order. The resulting API is harder to audit as
new domains are added.

The Sovetwave engineering guidance recommends root-level domain umbrellas,
explicit public entry points, and an acyclic dependency graph. The current
include graph is already acyclic, so the immediate problem is ownership and
public-boundary clarity rather than a behavior rewrite.

## Decision

1. Each complete domain will expose a root-level
   `src/imtg/<domain>.hpp` umbrella.
2. Domain leaves remain under `src/imtg/<domain>/`; cross-domain includes use
   public `<imtg/...>` entry points and intra-domain includes use local paths.
3. Umbrellas include lower-level domains and their own components in a stable
   topological order. Leaves never include their own umbrella.
4. Public standalone headers are self-contained. Private leaves may rely on a
   documented umbrella context, but are not installed or tested as public API.
5. The install manifest and public-header tests are explicit. Recursive install
   is replaced only by a reviewed implementation PR.
6. The migration is staged: documentation/audit first, then one domain or
   tightly coupled slice per implementation PR. Existing installed paths are
   not removed without a declared compatibility decision.
7. The actions registry and optional commerce domain keep their current
   semantic boundaries until their own focused changes are reviewed.

The normative rules and current graph are maintained in
[header-architecture.md](../guides/header-architecture.md).

## Consequences

Positive consequences:

- consumers get one discoverable entry point per domain;
- dependency direction and cycles can be checked at the domain boundary;
- private implementation details can stop leaking through installation;
- migrations remain reviewable and do not change runtime behavior.

Costs and limits:

- temporary compatibility paths may exist during a declared migration window;
- each new domain needs an install and standalone-header test contract;
- the current repository remains on the old layout until implementation PRs
  land, so this ADR is not evidence that the migration is complete.

## Rejected alternatives

1. **Keep nested umbrellas indefinitely:** preserves accidental dependence on
   physical layout and does not establish one domain entry point.
2. **Mechanically split every type into a file immediately:** creates churn,
   artificial DTO headers, and avoidable include coupling before ownership is
   audited.
3. **Install every header forever:** exposes private leaves and makes the
   package contract impossible to narrow safely.

