# ZFSin kernel-heap corruption investigation (SSV-26770 / SSV-26896)

**Status:** one root cause found and fixed (§5). An unnecessary rewrite bundled into that
same fix has been reverted (§6). **The cause of the latest crashes (F/G) is still
unidentified** — three hypotheses have already been wrong (§8), so the next step is
instrumentation, not another theory (§10).

**Purpose of this document:** self-contained context recovery. It records what was
changed, every crash analysed, what was proven vs. merely suspected, which hypotheses
were **disproven** (so nobody re-treads them), and the reference facts that were
expensive to establish. Written 2026-08.

---

## 1. Repos, branches, commits

| Item | Value |
|---|---|
| ZFSin repo | `C:\Checkout\openzfscmd` → `origin` = `github.com/DataCoreSoftware/openzfs`, `upstream` = `openzfsonwindows/openzfs` |
| DataCore drivers repo | `C:\Checkout\datacore-sds-another\datacore-sds` |
| DSM/MPIO repo | `C:\Checkout\NewWik\wik` |
| Base branch | `rel-10psp22` |
| `79b7320ad` | Squash-merge of the SSV-26770 ZFSin CodeQL remediation (PR #113) onto `rel-10psp22`. Contains the four original commits `b8173063b`, `0fdfeb109`, `7c144d956`, `fdab61f86`. |
| **`SSV-26896-fix`** | Branch pushed to `origin`. Single commit **`c7a780d5a`** — the `kmem_vasprintf` fix (§5) plus the `__dprintf` rewrite (§6, **now known to be harmful**) plus a `types.h` warning comment. |
| Tested build | cbuf reports `zfs-0.8.0-2224-gc7a780d5a-dirty` — i.e. the crashing build **does** contain `c7a780d5a`, plus unidentified uncommitted local changes (`-dirty`). |

Scoped CodeQL runner: `contrib/windows/codeql/Invoke-CodeQLZFSinAnalysis.ps1`
(builds only the `ZFSin` target to avoid dual-compilation noise; exit code = finding count).

---

## 2. Background: what SSV-26770 changed

A CodeQL `mustfix.qls` remediation took the ZFSin driver from 223 Must-Fix findings to 0.
Three change families:

1. `ExAllocatePoolWithTag` → `ExAllocatePoolUninitialized` (~30 sites).
2. `strcpy`/`strncpy`/`strcat` → `strlcpy`/`strlcat`; `sprintf`/`sscanf`/`_snwprintf` in
   Windows-only files → `RtlStringCb*`/`RtlStringCch*`.
3. **The root-cause change that matters here:** the Windows-only macros
   `#define snprintf _snprintf` / `#define vsnprintf _vsnprintf` in
   `include/os/windows/spl/sys/types.h` were replaced with wrappers
   `zfs_snprintf`/`zfs_vsnprintf` built on `_vsnprintf_s(..., _TRUNCATE, ...)`, plus a
   new `zfs_vscprintf()` for the "measure without writing" idiom.

### 2.1 Critical fact: the allocator swaps in ZFSin are no-ops

In WDK 10.0.19041.0 (`km/wdm.h` ~line 23363):

```c
FORCEINLINE PVOID ExAllocatePoolUninitialized(PoolType, NumberOfBytes, Tag)
{ return ExAllocatePoolWithTag(PoolType, NumberOfBytes, Tag); }
```

So **every `ExAllocatePoolWithTag` → `ExAllocatePoolUninitialized` swap in ZFSin is
compile-time identical** — same code, same (non-)zeroing. These can be permanently
excluded as a cause of any ZFSin crash.

**Do not confuse this with the DataCore repo**, where the change was
`ExAllocatePoolZero` → `ExAllocatePoolUninitialized` (commit `0b23a33fe`), which *did*
drop zeroing and required `RtlZeroMemory` to be restored (`eaa1e602b`). Different repo,
different change, real risk.

### 2.2 The `zfs_vscprintf` hazard (the origin of everything in §5–§6)

Kernel mode has **no linkable way to measure a format's length without writing it**:

- `_vscprintf` — declared in the WDK headers but **not exported by the kernel-mode CRT
  import library** (confirmed by an actual LNK2001 link failure).
- `_vsnprintf_s` — rejects `count == 0` (invokes the invalid-parameter handler).

So `zfs_vscprintf()` measures by formatting into a bounded **1024-byte scratch buffer**
and returns `1023` for anything longer. The original code's `_vsnprintf(NULL, 0, ...)`
returned the **true, unbounded** length.

**Consequence:** any `measure → allocate → write` sequence silently under-allocates for
long strings. The original in-code comment claimed this was harmless because "the later
write truncates identically" — **that reasoning is wrong** and directly caused §5. It has
been replaced with an explicit warning in `types.h`.

---

## 3. Crash inventory

All ZFSin unless noted. Build path is always `C:\BuildAgent\work\e347f52f66de7020\...`.

| # | Signature | Where | Verdict |
|---|---|---|---|
| **A** | `nvlist_free` GP fault, `nvpair.c:881` (`curr = curr->nvi_next`). Node 27 of a 37-entry list had `nvi_next` = `0x656d616e74736f68` = ASCII **"hostname"**, `_nvi_hashtable_next` = **"Windows\0"**. Path: `dispatcher` → `ioctlDispatcher` → `zfsdev_ioctl` → `zfsdev_ioctl_common` → `nvlist_free(innvl)` (`zfs_ioctl.c` ~7858). `zpool.exe`, VMware. Pool had been destroyed/exported shortly before. | ZFSin | **Explained by §5** (multi-owner memory) |
| **B** | `CScsiPort::ScsiControl` AV writing to `0xb`; `pSrb` = `8`. PnP `_AddDevice` path. | **DcsSp** | **Root-caused, separate bug.** See §7 and `datacore-sds/Tools/Docs/DcsSp-BSOD-RCA-IOCTL_GET_PORT_INTERFACE.md` |
| **C** | `memcpy` AV reading NULL — `abd_copy_to_buf_off_cb` (`abd.c:829`). `zvol_os_write_zv` → `dmu_tx_check_ioerr` → `arc_read` → `zio_decompress_data`. `rdx` = exactly `-rcx`, so **src = NULL** (not overflow). Scatter ABD chunk pointer was NULL. HyperV, `ReplaceRaidDiskVerifyDataIntegrity`. | ZFSin | Consistent with §5 (clobbered ABD chunk array) |
| **D** | `vmem_hash_delete` panic "bad free" (`spl-vmem.c:796`, `vsp == NULL`). `txg_sync_thread` → `spa_sync` → `dsl_scan_sync` → `ddt_sync` → `ddt_object_destroy` → `arc_hdr_destroy` → `abd_free_struct_impl` → `vmem_xfree`. Garbage 6.4 MB size. | ZFSin | Consistent with §5 |
| **E** | **Driver Verifier.** `nvlist_free` → `nvp_buf_free` (`nvpair.c:884`) → `vmem_xfree` → `vmem_hash_delete` "bad free". **Same ioctl stack as A.** | ZFSin | **This dump led to finding §5** |
| **F** | `kmem_error` → `KMERR_BADCACHE`. `kmem_cache_reap` → `kmem_depot_ws_reap` → `kmem_magazine_destroy`. `kmem_panic_info`: `kmp_error=6`, `kmp_cache`=`kmem_alloc_256`, `kmp_realcache`=`kmem_alloc_384`, `kmp_bufctl=NULL`. Build confirmed to contain `c7a780d5a`. | ZFSin | **§6 — regression from the §5 fix** |
| **G** | Identical to F, reproducible. Buffer dump showed multi-owner contents (see §6.2). | ZFSin | Same as F |

---

## 4. Ruling out the remediation, per crash

For every crash, each file in the stack was checked against the four remediation commits:

- **Crash A/E path:** `module/nvpair/nvpair.c` — **never touched**. `zfs_ioctl.c` — only
  `zfs_get_parent()` (`strncpy`→`strlcpy`), a different function. `zfs_ioctl_os.c` — only
  `zpool_zfs_get_metrics()`, a different function.
- `BufferUserBuffer()` in `zfs_vnops_windows.c` *was* rewritten by the remediation, but is
  **not in this call path** — `zc`/`innvl` arrive via `kmem_zalloc` + `copyin()` +
  `get_nvlist()`'s own `ddi_copyin`, not `BufferUserBuffer`. **Ruled out.**
- **Crash C/D paths:** `abd.c`, `abd_os.c`, `arc.c`, `dbuf.c`, `dnode.c`, `dmu_object.c`,
  `ddt.c`, `dsl_scan.c`, `spa.c`, `txg.c`, `spl-vmem.c`, `zio_compress.c`, `dmu_tx.c`,
  `zvol_os.c` — **none touched**. `zfs_windows_zvol_scsi.c` and `spl-taskq.c` *were*
  touched but only in unrelated functions (`ScsiOpInquiry`; `taskq_create_common`'s
  `tq_name`, not `taskq_thread`), plus no-op allocator swaps per §2.1.

---

## 5. ROOT CAUSE (fixed): use-after-free + double-free in `kmem_vasprintf()`

### 5.1 The defect

`kmem_vasprintf()` used measure-then-allocate. Once `zfs_vsnprintf(NULL, 0, ...)` began
returning a **capped 1023** (§2.2), a previously **unreachable** error path became live for
any format producing ≥1024 characters:

```c
size = zfs_vsnprintf(NULL, 0, fmt, ap);        /* capped at 1023 */
ptr = kmem_alloc(size + 1, KM_SLEEP);          /* 1024 - too small */
r = zfs_vsnprintf(ptr, size + 1, fmt, ap);     /* returns -1 (truncated) */
if ((r < 0) || (r > size)) {
        kmem_free(ptr, size);   /* 1023 vs allocated 1024 - mismatched size */
        r = -1;                 /* r is discarded; ptr NOT cleared */
}
return (ptr);                   /* returns the FREED pointer */
```

Before the remediation the measurement was exact, so the write always returned exactly
`size` and this branch was dead code.

### 5.2 Why it was catastrophic

`log_internal()` (`module/zfs/spa_history.c:536`):

```c
msg = kmem_vasprintf(fmt, adx);
fnvlist_add_string(nvl, ZPOOL_HIST_INT_STR, msg);  /* use-after-free READ */
kmem_strfree(msg);                                 /* DOUBLE FREE, garbage size */
```

`kmem_strfree` computes `strlen(msg) + 1` on freed memory, so the second free uses an
arbitrary size. Reached from `spa_history_log_internal()`, which logs nearly every pool
operation — hence corruption surfacing later in unrelated subsystems (nvlist teardown,
ABD teardown), matching crashes A, C, D, E.

### 5.3 The fix (in `c7a780d5a`, keep this)

`kmem_vasprintf()` now grows a scratch buffer and retries the real write — no measurement
pass — then returns a copy allocated at **exactly `strlen + 1`**. `kmem_asprintf()`
delegates to it.

**Hard invariant that constrains any future rewrite:** callers free these with
`kmem_strfree()`, which is `kmem_free(str, strlen(str) + 1)`
(`spl-kmem.c:6577`, and a macro at `zfs_context.h:694`). Returning the grown power-of-two
buffer directly would reintroduce mismatched frees at **all ~25 `kmem_asprintf` call
sites**. Hence the final exact-size copy.

---

## 6. `__dprintf` — unnecessary rewrite, now REVERTED

> **Theory withdrawn.** An earlier version of this document asserted that
> `__dprintf` reentrancy into the allocator *caused* crashes F/G. **That is not
> established.** Reentrancy produces deadlock, not `KMERR_BADCACHE`, and the
> original `__dprintf` also allocated (one `kmem_alloc`) without crashing. The
> claim was pattern-matching, not proof. The rewrite was reverted because it was
> unjustified scope creep in a dangerous path — not because it was proven guilty.
> **The actual cause of F/G remains unidentified; see §10.**

### 6.1 Why the rewrite was wrong regardless

Fixing §5 did not require touching `__dprintf` at all. The rewrite bought
untruncated debug messages — a cosmetic gain — at the cost of turning 1
allocation into 6–8 in the most reentrancy-sensitive path in the driver:

The same commit also rewrote `__dprintf()` to build its message with
`kmem_vasprintf()` + `kmem_asprintf()`. But:

- `module/os/windows/spl/spl-kmem.c` calls `dprintf()` **22 times**
- `module/os/windows/spl/spl-vmem.c` calls `dprintf()` **54 times**
- `kmem_error()` itself calls `dprintf()` (lines 955, 965, 966, 970)

**The allocator logs through the debug logger, and the debug logger was made to allocate.**

| | kmem operations per debug message |
|---|---|
| Original `__dprintf` | 1 `kmem_alloc` + 1 `kmem_free` (formatting via non-allocating `snprintf`) |
| After `c7a780d5a` | `kmem_vasprintf` (alloc + grow loop + alloc + free) + `kmem_asprintf` (same again) + `kmem_strfree` + `kmem_free` ≈ **6–8 ops** |

Any `dprintf` issued from inside `vmem_xalloc`, `kmem_slab_alloc`, or the magazine/depot
layer — frequently **while holding `vm_lock`/`cache_lock`** — now triggers 6–8 reentrant
allocator operations. ZFS's kmem/vmem is not reentrant on those paths. This is a direct
mechanism for freelist corruption, and it also means the diagnostics perturb the very heap
being diagnosed.

### 6.2 Evidence (crash G buffer dump)

One 384-byte buffer simultaneously contained: `"%recv\0"`, a GUID tail
`"21-5939-4cb4-b3bb-fb40a9530e43\0"`, a `dsl_scan` dbgmsg fragment
(`"nned dataset 45 (Z.a1b513f7-.../$ORIGIN) with min=3 max=1640; suspending=0"`), a
`metaslab_load` dbgmsg fragment, several kernel pointers, and `0xbaddcafe`
(`KMEM_UNINITIALIZED_PATTERN`) filler. **Four-plus distinct owners in one buffer.**

Size arithmetic corroborates the dbgmsg path:

```
zfs_dbgmsg_t = zdm_node(16) + zdm_timestamp(8) + zdm_size(4) + zdm_msg[1]
             → offsetof(zdm_msg) = 28, sizeof = 32
__zfs_dbgmsg(): size = 32 + strlen(msg)
   strlen ~220 → 252 → kmem_alloc_256
   strlen ~350 → 382 → kmem_alloc_384    ← exactly the two caches in KMERR_BADCACHE
```

**Why `BADCACHE` is a downstream symptom, not the origin:** `zdm_size` occupies offset
24–27, which in the dumped buffer holds ASCII `"0e43"`. Once a `zfs_dbgmsg_t` header is
overwritten, `zfs_dbgmsg_purge()` calls `kmem_free(zdm, <garbage>)`, freeing to an
arbitrary wrong cache — precisely `KMERR_BADCACHE`.

### 6.3 What was actually done: minimal revert

`__dprintf` was reverted to its original **single-allocation** structure, keeping only
the two genuine off-by-one corrections. Substantive delta vs. the shipping code is now
three lines:

| Was | Now | Why |
|---|---|---|
| `snprintf(buf, size + 1, ...)` | `snprintf(buf, size, ...)` | `buf` holds exactly `size` bytes |
| `zfs_vsnprintf(buf + i, size - i + 1, ...)` | `zfs_vsnprintf(buf + i, size - i, ...)` | ditto (found by external review) |
| `i = snprintf(...)` | `i = (int)strlen(buf)` | on truncation `_vsnprintf_s` returns **-1**, and `buf + (-1)` is a wild pointer. A truncating write still null-terminates, so `strlen` is always the true prefix length and always leaves `size - i >= 1`. Latent hazard in the original. |

`kmem_alloc(size)` / `kmem_free(buf, size)` symmetry restored.

**A rejected alternative, recorded so it is not retried:** making `__dprintf`
allocation-free with a fixed stack buffer plus a per-thread recursion guard. That is
*more* new code in the same dangerous path, justified by the theory withdrawn above.
Not warranted. Note `__zfs_dbgmsg()` still does `kmem_zalloc` — a pre-existing
reentrancy that predates the remediation and is deliberately left alone.

### 6.4 The invariant that makes the capped measurement safe here

`__dprintf` *does* size its allocation from `zfs_vscprintf`'s capped value, which is
fine because it satisfies both halves of the rule now documented in `types.h`:

1. every write is bounded by the buffer's real size (not by the measured length), and
2. the free passes the same size that was allocated.

A capped measurement then costs only truncated message text. §5 was unsafe precisely
because it violated both.

---

## 7. Second confirmed root cause (different driver): DcsSp `IOCTL_GET_PORT_INTERFACE`

Pre-existing, unrelated to the remediation. Full write-up:
`datacore-sds/Tools/Docs/DcsSp-BSOD-RCA-IOCTL_GET_PORT_INTERFACE.md`.

Chain: `IRP_MJ_SCSI` and `IRP_MJ_INTERNAL_DEVICE_CONTROL` are **the same value `0x0f`**
(`km/wdm.h`), so `DcsSp`'s `IRP_MJ_SCSI` handler also receives internal IOCTLs.
`CDriverShim::GetScsiPortInterface()` sends the custom `IOCTL_GET_PORT_INTERFACE` with
`OutputBufferLength = sizeof(ppPort) = 8`. `CScsiPort::ScsiControl()` reads
`Parameters.Scsi.Srb`, which **aliases** `Parameters.DeviceIoControl.OutputBufferLength` in
the `IO_STACK_LOCATION` union → `pSrb = 8`, passes the `!pSrb` NULL check, then writes
`SrbStatus` at `[8+3] = 0xb`.

`IOCTL_GET_PORT_INTERFACE` is referenced **nowhere** in `SpDriver`. Four sibling drivers
(`NVMeTCPDriver`, `iScsiServerDriver`, `iScsiManagerDriver`, `iScsiIsp4KDriver`) handle it
correctly via an `OnInternalIoctl()` override that checks `IoControlCode` first.
`CScsiPort` derives from `IScsiPort`, not `CFdo`, so it never joined that convention.
Also note `_AddDevice` probes both SCSI and FC devices because
`FILE_DEVICE_SCSI_PORT == FILE_DEVICE_FCP_PORT`.

Environment correlate: the crashing VM had an **LSI Logic SAS** vSCSI controller
(`lsi_sas.sys` loaded). Not yet confirmed whether non-crashing VMs differ — but every
DataCore-fronted SCSI-port device is exposed, and it only fires on a PnP add/re-enumeration.

---

## 8. Disproven / superseded hypotheses — do not re-tread

| Hypothesis | Status |
|---|---|
| **`spa_add_feature_stats()` / `spa_feat_stats` missing-lock race** as the cause of crash A | **WRONG.** Superseded by §5. The file `contrib/windows/docs/ZFSin-BSOD-RCA-spa_feat_stats.md` (untracked) states this conclusion and **should be deleted**. `nvlist_add_nvlist` does a genuine deep copy via `nvlist_copy_embedded`, so there is no aliasing there. The unlocked `nvlist_free(spa->spa_feat_stats)` in `spa_remove()` (`spa_misc.c:825`) is still a real asymmetry worth fixing defensively, but it is not the cause. |
| `BufferUserBuffer()` (the `FsRtlAllocatePoolWithQuotaTag` rewrite) causing crash A | Ruled out — not in that call path (§4). |
| `zfs_dbgmsg_fini()` as the cause of `KMERR_BADCACHE` | Wrong — it only runs at driver unload, and its recomputed size matches in the normal case. It *is* a latent antipattern worth fixing (see §9). |
| Crash C being "pointer-arithmetic overflow" | Imprecise. `rdx` is exactly `-rcx`, so `[rcx+rdx]` is MSVC `memcpy`'s addressing idiom and the source pointer is a **clean NULL**, not a wrapped valid pointer. |
| An off-by-one free size producing `KMERR_BADCACHE` | Not possible — ZFS buckets caches, so 1023 and 1024 both resolve to `kmem_alloc_1024`. `BADCACHE` requires a **bucket-crossing** discrepancy. |
| ZFSin allocator swaps changing behaviour | Impossible, see §2.1. |

---

## 9. Other real findings, not yet fixed

1. **`zfs_dbgmsg_fini()`** (`zfs_debug.c:141`) recomputes the free size as
   `sizeof(zfs_dbgmsg_t) + strlen(zdm->zdm_msg)` while `zfs_dbgmsg_purge()` (line 91)
   correctly uses the stored `zdm->zdm_size`. Same "derive the free size from mutable
   data" antipattern as §5. Fix defensively.
2. **`abd_alloc_chunks()`** (`abd_os.c:184-187`) stores `kmem_cache_alloc()` results into
   `abd_chunks[i]` with **no NULL check**, although `abd_verify_scatter()`
   (`abd_os.c:174-177`) asserts them non-NULL — and `ASSERT3P` compiles out in Release, so
   the guard is inert in shipping builds. This is an unguarded path to crash C's symptom.
3. **`spa_remove()`** (`spa_misc.c:825`) frees `spa->spa_feat_stats` without
   `spa_feat_stats_lock`, unlike every other access. Cheap defensive fix.
4. Other `kmem_free(p, strlen(p) + 1)` sites that would break if the string is ever
   mutated: `spa_misc.c:1513`, `include/os/windows/spl/sys/sid.h:68`,
   `module/icp/os/modhash.c:230,237,528`.

---

## 10. Recommended next steps, in order

1. **Stop theorising and instrument.** Three hypotheses have already been wrong (§8);
   each costs a build/install/test cycle. Run a build with full kmem auditing. In
   `spl-kmem.c`, `kmem_flags` is `KMF_LITE`; the full set is commented out one line above:
   ```c
   int kmem_flags = KMF_DEADBEEF | KMF_REDZONE | KMF_CONTENTS | KMF_AUDIT;
   ```
   With `KMF_AUDIT`, `kmp_bufctl` is populated and `kmem_error()` dumps the **previous
   transaction's thread and call stack for that exact buffer** — naming the offending
   free directly instead of guessing across ~80 candidate sites.

   **Caveat, per the in-tree comment above that line: `KMF_AUDIT` never releases the
   audit records, so the machine will eventually grind to a halt.** It is a
   bounded-repro diagnostic only and must not ship. For that reason it lives on its own
   throwaway branch (`SSV-26896-kmem-audit-diag`), never on `SSV-26896-fix`.
   Note `kmem_flags` is inside `#ifdef DEBUG`, so the audit build must be a DEBUG build.
2. Establish what the `-dirty` in `zfs-0.8.0-2224-gc7a780d5a-dirty` was — the tested
   binary contained uncommitted changes beyond `c7a780d5a`, which is a hole in the
   evidence chain for every conclusion drawn from crashes F/G.
4. Re-run `Invoke-CodeQLZFSinAnalysis.ps1` after any fix to confirm still 0 Must-Fix.
5. Fix the §9 items.
6. Decide on the DcsSp fix (§7) — add an `IoControlCode` check in `PortScsiControl`
   mirroring `CIsp4k::OnInternalIoctl()`.

---

## 11. WinDbg cookbook (commands that actually paid off)

```
!analyze -v
.cxr <Context Record Address>          ; !analyze resets scope afterwards - re-enter it
kb                                     ; stack with args, after .cxr

.sympath+ <pdb folder>                 ; then:
.reload /f ZFSin.sys

u <func> L20                           ; map registers to source vars via disassembly
                                       ; (needed when private symbols are missing and
                                       ;  `dv` fails with "Private symbols required")

dt ZFSin!kmem_panic_info               ; THE command for any kmem_error panic:
                                       ; kmp_error/buffer/realbuf/cache/realcache/slab/bufctl
dt ZFSin!kmem_cache_t <addr> cache_name cache_bufsize
db <kmp_buffer> L180                   ; buffer contents - identifies the owner(s)

dt ZFSin!cbuf                          ; 1 MB circular debug log; contains the
s -a <cbuf addr> L100000 "bad free"    ; software version string and dprintf output
.writemem C:\cbuf.txt <cbuf addr> L100000

; walk an nvlist's i_nvp_t chain when !list can't resolve the field offset
; (nvi_next is at offset 0, per the nvlist_free disassembly):
r $t0 = <priv->nvp_list>
r $t1 = 0
.for (; @$t0 != 0; r $t1 = @$t1 + 1) { .printf "node %d: %p\n", @$t1, @$t0; r $t0 = poi(@$t0) }
```

Notes:
- `!list -t ZFSin!i_nvp_t.nvi_next -e <addr>` fails (`GetFieldOffset failed`), and this
  build's `!list` rejects a raw numeric offset — use the `.for` loop above.
- WinDbg's `&&` in a `.for` condition is a parse error; drop the compound condition.
- `kmem_error()` **re-derives** its error code (`spl-kmem.c:855`), overriding the value its
  caller passed. `spl-kmem.c:976` is the common `DbgBreakPoint()` exit for all nine codes,
  so the faulting line number tells you nothing about which check failed — always read
  `kmem_panic_info`.
- Error codes: `MODIFIED 0, REDZONE 1, DUPFREE 2, BADADDR 3, BADBUFTAG 4, BADBUFCTL 5,
  BADCACHE 6, BADSIZE 7, BADBASE 8`.
- Registers in `kmem_error`'s frame have consistently held: `rdx`/`rbp` = error code,
  `rsi` = slab, `r12` = buffer, `rdi` = realcache.

---

## 12. Process lessons

- The `__dprintf` off-by-one and the `kmem_vasprintf` UAF were **the same bug class**: a
  capped measurement turning a dormant latent bug into a live one. After an external
  reviewer found the first instance, the other two consumers of the measurement path
  (`kmem_asprintf`, `kmem_vasprintf`) were not audited. **When changing a shared
  primitive's contract, enumerate and audit every consumer.**
- "Compiles clean + CodeQL clean + driver-scoped scan at 0 findings" validated nothing
  about cross-function invariants. A full multi-target build later surfaced `LNK2001`
  errors, and Driver Verifier surfaced the UAF.
- Several hypotheses in this investigation were confidently wrong (§8). Register-based
  inference in particular proved unreliable until cross-checked against
  `kmem_panic_info`. Prefer one decisive command over three plausible theories.
