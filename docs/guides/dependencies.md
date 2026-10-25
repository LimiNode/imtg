# Dependencies

Core must remain backend- and transport-neutral. Integrations are adapters behind imtg contracts: MDBX through `mdbx-containers`, HTTP through `kurlyk`, and event delivery through `event-hub-cpp` where its semantics fit. Dependency additions require an ownership/license/API review, a documented failure model, and a conformance or fake-based test plan. D0 adds no dependencies or submodules.
