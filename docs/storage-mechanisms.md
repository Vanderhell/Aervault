# Storage modules and upstream mechanisms

Storage features are opt-in static CMake targets. The shared Aervault journal
is an append-only, commit-last record stream in two erase-aligned banks. Records
are CRC checked, program/commit operations are synced separately, and a torn
tail blocks further append until compaction publishes a verified replacement
bank. WAL, history, queue and diagnostic records share this stream.

| Aervault target | Mechanism used | Source and pinned upstream revision | License / integration |
| --- | --- | --- | --- |
| `aervault::nor` | Partition geometry, bounds, programming/erase alignment | `microflash/include/mflash.h`, `microflash/src/mflash.c`, `7730387307451db9c86c42e9681263f135949cce` | MIT. Used unmodified through `microflash::microflash`; Aervault binds the partition to journal callbacks and requires an explicit sync callback. |
| `aervault::journal` | Separate active/inactive bank and publish only after a durable commit | `loxdust/src/hd_commit.c`, `975cb56388b5a466a7a4056fdab654d299984aee` | MIT confirmed in that revision's `LICENSE`. The object-layout-specific implementation does not fit the journal API; Aervault implements the bank-switch boundary for its own record format. |
| `aervault::wal` | Prepare-before-data, finish record, replay/undo after recovery | `loxdb/src/lox_wal.c`, `f373bb499ae6f6ff6c2b5cd38b59e237a56c612a` | MIT. The database WAL API is internal to loxdb; Aervault implements the smaller single-key before/after WAL over its journal. |
| `aervault::queue` | Stable monotonic IDs, enqueue/ACK records and pending-only recovery | `iotspool/include/iotspool.h`, `iotspool/src/record.c`, `0baab5f867f83cb3d31bad3057c1c1cb658b9b86` | MIT. Its record/API model was used as the template. The full public API was not linked because compaction allocates dynamically; the smaller Aervault queue is caller-owned and shares the journal. Network/MQTT remains outside Aervault. |
| `aervault::history` | Append confirmed values, iterate verified entries, bounded retention | `nvlog/include/nvlog.h`, `nvlog/src/nvlog.c`, `c6d5e7566d31ca13b30d22851e67e606d7cf0860` | MIT. nvlog's ring API is restricted to byte-writable media; Aervault ports the bounded-retention selection to the shared commit-last journal, which also supports NOR. |
| `aervault::budget` | Preflight admission and explicit denial reason | `loxbudget/include/loxbudget.h`, `loxbudget/src/loxbudget_core.c`, `584351686f0b97549a09a055d15bf2aca262d046` | MIT. Its broader resource engine is not needed for this small fixed write budget. Aervault tracks program and erase usage separately; `av_nor_write_counts()` reads microflash's separate write/erase counters. |
| `aervault::diag` | Fixed event wire layout, integrity check, final commit marker, bounded event retention | `panicdump/src/panicdump_wire.h`, `panicdump/src/panicdump.c`, `a7808dbd8b2e8e3039c80d687e9684ef13f4a43f`; `nvlog/src/nvlog.c`, `c6d5e7566d31ca13b30d22851e67e606d7cf0860` | MIT for both. panicdump's Cortex-M crash-capture interface is not a portable persistent event-log API; Aervault uses its fixed-field/commit-last pattern with journal CRC and bounded retention. |

All upstream revisions above declare MIT terms; loxdust's repository metadata
does not classify its license, so the revision's `LICENSE` file was read and
confirmed explicitly. No upstream tests are added to or run by Aervault's test
configuration. The queue adapter's host regression exercises Aervault's
adapter against the pinned public API; it is not the iotspool test suite.

## CMake selection

Enable only the targets needed by the application. `AV_ENABLE_NOR` fetches and
builds only microflash's library source and header, not its test targets.
`AV_ENABLE_WAL`, `AV_ENABLE_QUEUE`, `AV_ENABLE_HISTORY` and `AV_ENABLE_DIAG`
each link the shared `aervault::journal` target. No storage target is pulled in
by core-only builds.
