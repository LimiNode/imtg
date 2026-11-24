# MSVC allocation-failure harness diagnosis

**Status:** local diagnosis verified; exact hosted toolset confirmation remains
a gate. **Owner:** storage test harness. This changes test injection, not the
MDBX production implementation or its exception guarantee.

The original unrestricted sweep at `create_absent/index=4` reached a
`noexcept` default string constructor in `encode_physical_key`, not native
MDBX rollback or locking. Local MSVC 19.51.36260, STL 145, iterator level 2,
C++17 `/EHsc /Zi /Ob0 /Od /RTC1 /MDd` reproduced it. The standalone default
string probe and the actual mutation diagnostic both exited 77 from a custom
terminate handler. Fixed-buffer OS writes and stack capture do not allocate.
MAP/PDB evidence resolves the injection stack to:

```text
test_allocation_failure_rolls_back_staging
  create_if_absent -> read_state -> encode_physical_key
    basic_string() -> _Construct_empty -> _Alloc_proxy
      allocator<_Container_proxy>::allocate -> operator new -> bad_alloc
```

The installed xstring default constructor is `noexcept` and calls
`_Construct_empty`; xmemory allocates its iterator proxy. Here the proxy is
16 bytes. A failed allocation in that path invokes terminate by the language
contract, rather than propagating to the mutation test's catch. The unfiltered
diagnostic retains the original short key and index, with no production change.

The ordinary MSVC iterator-debug test now sweeps larger, recoverable allocations
while retaining iterator diagnostics. It excludes sizes at or below that STL's
proxy size (including any other allocation in that tiny size class). It does
not claim to test recoverable failure of every global new. Other profiles keep
the unrestricted sweep. It still requires rollback of the entire transaction,
including unrelated prior writes, terminal read/scan/commit rejection, and fresh
point/scan/watch agreement for every injected exception. The existing seven
mutation scenarios and common ST-2 cases remain active in Debug and Release.

Local ordinary MSVC Debug MDBX CTest: 1/1 passed after the harness correction.
Local raw evidence stays under `build/msvc-debug/bin/` (MAP/PDB), with the run
log under the workspace `build/msvc-fault-diagnostic/`. Hosted Windows Debug
prints the exact compiler/STL/iterator settings, compiler command records and
installed-header excerpts; its artifact retains MAP/PDB and CTest logs. Hosted
confirmation must be reported by exact SHA/run, separately from this local result.

No timeout increase, ordinal skip, iterator-diagnostics disablement, production
workaround, wire-format change or alteration of Unknown semantics is involved.
