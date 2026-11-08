# Public API documentation

**Status:** normative. **Semantic owner:** maintainers.

Every installed public type and every nontrivial public operation must carry semantic English
Doxygen documentation. New public API documentation is delivered in the same change as the API.

Document the guarantees a consumer needs to use the API correctly:

- ownership, borrowed-object lifetime, and invalidation;
- thread-safety, callback delivery, reentrancy, and quiescence;
- success, error, conflict, and unknown-outcome semantics;
- persistent identities, revisions, expected-version checks, and recovery boundaries;
- side effects and meaningful complexity or memory bounds.

Avoid comments that merely restate a declaration. Public fields should explain their role or
invariant. Every parameter and non-void return value must be documented. Internal implementation
details stay in ordinary comments unless they form part of a public contract.

## Header entry points

Each complete domain has one documented umbrella entry point at
`<imtg/<domain>.hpp>`. An umbrella documents the domain boundary and re-exports
owned public components; it does not duplicate declarations. Public standalone
headers are also valid first includes and must carry the documentation needed
to use their exposed types and operations. A private leaf may depend on the
umbrella context only when the architecture guide says so and the install
manifest excludes it.

The public-header test must compile every installed umbrella and every declared
standalone header as the first include. It must also prove that private leaves
are not installed. Include order, cross-domain entry points, and compatibility
shims are architectural contracts, not merely build conveniences. See
[header architecture](header-architecture.md) for the inventory and staged
migration policy.

## Validation

Doxygen remains optional for normal C++ configuration and builds. To run the strict public API
check explicitly:

```sh
cmake -S . -B build/documentation -DBUILD_TESTING=OFF -DIMTG_BUILD_DOCUMENTATION=ON
cmake --build build/documentation --target imtg_documentation
```

The generated configuration scans all installed headers under `src/imtg`, requires parameter
and return documentation, and fails on every warning. CI runs the same target in a dedicated job.
Do not suppress warnings globally to admit undocumented API; either document the contract or
narrow the generated input only when a file is genuinely not installed public API.
