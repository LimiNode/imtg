# Public API documentation

Every public type and function needs a semantic description before implementation is considered complete. State ownership, valid lifetime, thread-safety assumptions, failure results, side effects, and complexity where material. Examples must say `Conceptual` until the implementation status is `implemented`.

API review checks naming, include ownership, dependencies, stable identifiers, error semantics, and whether a callback can outlive its context. A public API change updates its owning contract and tests in the same PR.
