# MSVC allocation-failure harness diagnosis

**Status:** local and exact hosted diagnosis verified; corrected hosted suite
remains a separate gate. **Owner:** storage test harness. This changes test injection, not the
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

## Exact hosted confirmation and negative controls

[Run 45291273293, Windows Debug job 138100358331](https://github.com/LimiNode/imtg/actions/runs/45291273293/job/138100358331)
at diagnostic source `dec41c12ae70593e6a19f9ae9da1fed63645ffff` reports
MSVC 19.52.37029, STL 145, iterator level 2, proxy size 16, and a noexcept default
string constructor. Both the standalone probe and original unfiltered mutation
exit 77. The job's `mdbx-msvc-debug-diagnostics` artifact contains matching
MAP/PDB files. Mutation injection RVAs resolve as follows:

| RVA | MAP symbol (simplified) |
| --- | --- |
| `0x4de01` | `allocator<_Container_proxy>::allocate` |
| `0x2af6e` | `_Container_base12::_Alloc_proxy` |
| `0x48298` | `basic_string::_Construct_empty` |
| `0x3a1ce` | `basic_string::basic_string()` |
| `0x5442d` | `encode_physical_key` |
| `0x56ccb` | `Transaction::read_state` |
| `0x82aa4` | `Transaction::create_if_absent` |
| `0x16d92` | `test_allocation_failure_rolls_back_staging` |

The flags include `/EHsc /Zi /Ob0 /Od /RTC1 /MDd /std:c++17`. Installed
`xstring`/`xmemory` excerpts in that job corroborate the same path. Its ordinary
unfiltered suite still times out at index 4: this failed diagnostic run is not
corrected-suite acceptance. The filtered suite is checked in a later run.

With the current GNU 16.1 Debug test objects and the old implementation file
from `a5cdf3259792eb600f6bfb50cf78853bf568dc62`, separate control executables
fail respectively on terminal `RolledBack` after allocation index 0 and on
the `net-no-op` watch revision. Rebuilding against the corrected implementation
passes both storage suites in Debug and Release (2/2 each). This is an old-source
relink experiment, not a full baseline build. Artifacts remain under
`build/negative-control/`; no production implementation was changed to diagnose
the STL path.

No timeout increase, ordinal skip, iterator-diagnostics disablement, production
workaround, wire-format change or alteration of Unknown semantics is involved.
