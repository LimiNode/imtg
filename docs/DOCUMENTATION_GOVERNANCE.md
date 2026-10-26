# Documentation governance

**Status:** normative for D0. **Semantic owner:** maintainers of the imtg core contracts.

Normative contracts are the single source of truth. Overview documents explain relationships and link to the owning contract; they must not define a competing version. Every contract states purpose, ownership, inputs, guarantees, failures, concurrency/shutdown limits, dependencies, and verification.

Architectural examples are conceptual until a roadmap milestone marks the corresponding implementation as `implemented`. A public API change requires an updated contract, Doxygen policy review, implementation status entry, and tests in the same change. Significant trade-offs belong in an ADR with alternatives and open questions.

Canonical documents are written in English. Translations are informative and must identify the canonical source and synchronization date; they must not silently diverge or be presented as an official second contract.

Before review, check links, terminology, status claims, invariant references, and `git diff --check`. The current status is maintained in [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md).
