# DAT auditing & management

Contributor reference for the **DAT Manager** — the subsystem that audits a
folder of media against one or more DAT catalogues and reports/fixes the
differences. It builds on the DAT-file lookup layer
([dat-lookup.md](dat-lookup.md)); read that first for how DATs are parsed and
cached.

## What it does

Given some **scan folders** and some **DAT files**, an audit:

1. enumerates the files under the scan roots,
2. hashes each file (CRC32 / MD5 / SHA-1),
3. compares the hashes against the union of the DATs' entries, and
4. classifies every file *and* every catalogue entry into a status.

The result drives a filterable table, three export formats, and a gated
fix engine.

## Module map

All in `src/modules/data/dataudit/` (the `kartend_data` layer — depends only on
`kartend_utils` + sibling data modules, never on UI):

| File | Role |
|------|------|
| `dataudittypes.{h,cpp}` | `Status` taxonomy, `AuditRow`, `AuditSummary`, `summarize()` |
| `datauditcatalogue.{h,cpp}` | `Catalogue` — hash + name indexes over the DAT union |
| `datauditrunner.{h,cpp}` | `buildCatalogue()`, pure `classify()`, threaded `run()` |
| `datauditexport.{h,cpp}` | `toCsv()`, `toFixdat()`, `toMissList()` |
| `datauditfix.{h,cpp}` | `computeFixPlan()`, `applyFixPlan()`, `applyUndo()` |
| `datauditmodel.{h,cpp}` | `DatAuditModel` — `QAbstractTableModel` for the results view |

Supporting stores live in `src/utils/db/` (the `kartend_utils` layer):

| File | Role |
|------|------|
| `filehashcache.{h,cpp}` | `(path,size,mtime) → {crc,md5,sha1}` cache (schema v16) |
| `datauditprofile.{h,cpp}` | `DatAuditProfile` CRUD — durable audit profiles (schema v17) |

UI:

| File | Role |
|------|------|
| `src/ui/dialogs/dataudit/datauditdialog.{h,cpp}` | the DAT Audit window |
| `src/core/datauditcontroller.{h,cpp}` | owns the cached window; `openDialog()` |

The window launches from **File → DAT Audit…** via the same path as the scraper
(`MenuController::onRunDatAudit` → `MainWindow::openDatAuditDialog` →
`DatAuditController`).

## Status taxonomy

Two orthogonal axes (entry-satisfaction vs. file-disposition), collapsed into one
`Status` enum the model renders. The runner emits:

| Status | Meaning |
|--------|---------|
| `Have` | File present, hash matches an entry, name is canonical |
| `WrongName` | Content matches an entry (hash) but the on-disk name differs — **rename candidate** |
| `WrongHash` | A file's name matches an entry but its content does not — bad / different revision |
| `Duplicate` | File matches an entry already satisfied by another file |
| `Unknown` | File matches no catalogue entry |
| `Corrupt` | File could not be read/hashed |
| `Missing` | Catalogue entry no file satisfied |

`Unscanned` / `BadDump` are reserved (modelled, not yet emitted).

`classify()` is **pure** — no I/O, no threads — so the whole matrix is
exhaustively unit-tested (`tests/modules/dataudit/test_datauditrunner.cpp`).

## Engine flow

```
DatCache::Store  ──buildCatalogue()──▶  Catalogue (hash+name indexes)
scan roots  ──enumerate──▶  files
files  ──hash (concurrent, FileHashCache-backed)──▶  ScannedFile[]
(Catalogue, ScannedFile[])  ──classify()──▶  AuditRow[] + AuditSummary
```

`run()` runs synchronously on its calling thread (the window calls it from a
`QtConcurrent::run` worker). Hashing is fanned out across the global thread
pool; cache reads/writes stay on the calling thread for `QSqlDatabase` thread
safety. A `cancel` token (checked between files and inside `RomHasher`) aborts
promptly and returns the partial result with `cancelled = true`.

`run()` accepts an optional `QSqlDatabase*` for the `FileHashCache`; pass
`nullptr` to hash without caching (the v1 window does this — wiring a worker
connection is a tracked follow-up).

## Fix safety model

`datauditfix` embodies **Scan → Plan → Apply**:

- `computeFixPlan(rows, settings)` is **pure** — it returns a previewable list
  of `FixAction`s (Rename / Relocate / Quarantine) without touching disk.
- `applyFixPlan(plan, dryRun)` executes with rails: parent dirs are created, an
  existing *different* destination is never overwritten (skipped), and every
  move/copy is recorded in an undo log.
- `applyUndo()` reverses an apply (copies deleted, moves moved back).

Destructive categories default **off** (rename is the only default-on fix);
quarantine *moves*, never deletes. Managed-output relocation *copies* (originals
untouched).

## Exports

- **CSV** — flat, one row per `AuditRow`, RFC-4180 escaped.
- **fixdat** — a Logiqx-XML `<datafile>` of only the `Missing` entries; it
  round-trips back through `DatLookup::parseLogiqxDat`, so it feeds any DAT tool
  to fetch exactly the gaps.
- **miss list** — sorted, de-duplicated plain text.

## Profiles (schema v17)

`DatAuditProfile` persists a durable binding of DAT(s) + scan root(s) + audit/fix
settings, independent of collections (an optional `collectionUuid` link). The
v1 window does not yet drive runs from a saved profile — the store + schema are
in place for the profile-management UI follow-up.

## Tests

`tests/modules/dataudit/` and `tests/utils/db/`:
`test_datauditrunner`, `test_datauditexport`, `test_datauditfix`,
`test_datauditmodel`, `test_filehashcache`, `test_datauditprofile`, plus the
v16/v17 cases in `test_dbmigrations` and the enumeration cases in
`test_datcache` / `test_datlookup`. All DB-backed tests use a real in-memory
SQLite built through the production migration path (no DB mocking).
