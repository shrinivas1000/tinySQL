# B+Tree Database Storage Engine

A single-table database storage engine implemented in C, providing on-disk B+Tree indexing and crash-safe durability through a Write-Ahead Log (WAL).

The project implements the storage and execution layer directly: page management, B+Tree indexing with node splitting, and durable writes via a page-level WAL.

## Capabilities

- `insert id username email` — inserts a row (schema: `id` (uint32), `username` (≤32 characters), `email` (≤255 characters))
- `select` — scans all rows in key order
- `.btree` — prints the B+Tree structure
- `.exit` — flushes pending writes, truncates the WAL, and closes the database cleanly

## Architecture

### B+Tree Storage Layer
- Data is stored in 4KB pages.
- Only leaf nodes hold row data; internal nodes serve purely as routing nodes over child page numbers.
- Leaf nodes are linked (`next_leaf`) to support efficient sequential scans without traversing back up the tree.
- Inserts that overflow a node trigger a split, propagating up through internal nodes and promoting a new root when required.

### Write-Ahead Log (WAL)
- Implements a page-level WAL: each modified page is tracked with a dirty bit, and on commit the full 4KB page is appended to the WAL and flushed via `fsync` before the write is considered durable.
- On startup, `wal_recover` replays any pages remaining in the WAL into the main database file, providing crash recovery.
- On a clean `.exit`, the WAL is truncated after all dirty pages are flushed to the main file.

## Build

Requires GCC and a POSIX-compliant environment (uses `unistd.h` and `fcntl.h` for file I/O). No external dependencies.

From the repository root:

```bash
gcc main.c -o db
```
