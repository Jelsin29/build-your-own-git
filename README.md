# Build Your Own Git

A minimal git implementation written from scratch in C. No libraries, no shortcuts — just raw content-addressed storage, SHA-1 hashing, zlib compression, and the DAG structure that makes git work.

## Why

To understand what happens under the hood when you run `git add`, `git commit`, or `git log`. This project implements both the plumbing layer (content-addressed object store, refs, index) and porcelain commands (add, commit, status, branch, checkout) — plus an ncurses TUI for visual exploration.

## Building

### Prerequisites

- GCC with C17 support
- zlib (`libz-dev` / `zlib-devel`)
- OpenSSL (`libssl-dev` / `openssl-devel`)
- ncurses (`libncurses-dev` / `ncurses-devel`) — for the TUI

### Compile

```bash
make          # builds the mygit binary
make test     # runs all 96 tests across 7 suites
make clean    # removes build artifacts
make debug    # builds with AddressSanitizer + UBSan
```

## Usage

### Initialize a repository

```bash
$ ./mygit init
Initialized empty mygit repository in ./.mygit/

$ ./mygit init myproject
Initialized empty mygit repository in myproject/.mygit/
```

This creates the `.mygit/` directory with the same internal structure as real git:

```
.mygit/
├── HEAD              # "ref: refs/heads/master\n"
├── config            # INI-style config (user.name, user.email)
├── index             # DIRC v2 binary staging area
├── objects/
│   ├── info/
│   └── pack/
└── refs/
    ├── heads/
    └── tags/
```

### Hash a file (create a blob)

```bash
# Just compute the hash (no write)
$ echo "hello world" > hello.txt
$ ./mygit hash-object hello.txt
95d09f2b10159347eece71399a7e2e907ea3df4f

# Compute hash AND write to object store
$ ./mygit hash-object -w hello.txt
95d09f2b10159347eece71399a7e2e907ea3df4f
```

The `-w` flag writes the compressed blob to `.mygit/objects/95/d09f2b10159347eece71399a7e2e907ea3df4f`.

### Inspect objects

```bash
# Show object type
$ ./mygit cat-file -t 95d09f2b10159347eece71399a7e2e907ea3df4f
blob

# Pretty-print object content
$ ./mygit cat-file -p 95d09f2b10159347eece71399a7e2e907ea3df4f
hello world
```

`cat-file -p` works on all three object types: blobs, trees, and commits.

### List tree entries

```bash
$ ./mygit ls-tree <tree-sha>
100644 blob 95d09f2b10159347eece71399a7e2e907ea3df4f	hello.txt
040000 tree a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2	src

# Names only
$ ./mygit ls-tree --name-only <tree-sha>
hello.txt
src
```

### Create a commit

```bash
# Root commit (no parent)
$ ./mygit commit-tree <tree-sha> -m "initial commit"
e4a7c8f2d1b3a5c7e9f1a3b5c7d9e1f3a5b7c9d1

# Commit with a parent
$ ./mygit commit-tree <tree-sha> -p <parent-sha> -m "add feature"
f5b8d9e3c2a4b6d8f0a2b4c6d8e0f2a4b6c8d0e2
```

### Walk commit history

```bash
$ ./mygit log <commit-sha>
commit e4a7c8f2d1b3a5c7e9f1a3b5c7d9e1f3a5b7c9d1
Author: mygit <mygit@local>
Date:   Mon Mar 24 12:00:00 2026 +0000

    add feature

commit a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2
Author: mygit <mygit@local>
Date:   Mon Mar 24 11:00:00 2026 +0000

    initial commit
```

`log` follows the parent chain (the DAG) from the given commit back to the root.

### Interactive TUI

```bash
$ ./mygit tui
```

Launches an ncurses terminal interface with:
- **Dashboard** — ASCII art header, repo stats (object counts by type), action menu
- **Object Explorer** — browse all objects with type detection and size display
- **Object Detail** — inspect blobs (scrollable content), trees (entry list), commits (metadata)
- **Tree Browser** — navigate tree entries like a file manager, drill into subtrees with stack-based back navigation
- **Commit Log** — sorted timeline of all commits with detail inspection
- **Hash File** — browse filesystem and hash files as blobs

Navigation: `j`/`k` to move, `Enter` to select, `Escape` to go back, `q` to quit. Cross-view navigation: `t` from a tree detail opens the tree browser, `l` from a commit detail opens the commit log.

### Stage files and commit (porcelain)

```bash
# Stage files
$ ./mygit add hello.txt main.c

# Check status
$ ./mygit status
On branch master

Changes to be committed:
  new file:   hello.txt
  new file:   main.c

# Commit
$ ./mygit commit -m "initial commit"
[master (root-commit) a1b2c3d] initial commit
 2 files changed
```

### Refs and config

```bash
# Update a ref manually
$ ./mygit update-ref refs/heads/feature <commit-sha>

# List all refs
$ ./mygit show-ref
<sha> refs/heads/master
<sha> refs/heads/feature

# Get/set config
$ ./mygit config user.name "Jelsin"
$ ./mygit config user.email "jelsin@example.com"
$ ./mygit config user.name
Jelsin
```

### Branches and checkout

```bash
# List branches (* marks current)
$ ./mygit branch
  feature
* master

# Create a branch
$ ./mygit branch new-feature

# Switch branches (updates working tree + index)
$ ./mygit checkout feature
Switched to branch 'feature'

# Delete a branch
$ ./mygit branch -d feature
Deleted branch feature (was a1b2c3d)
```

## Complete workflow example

A full session showing both plumbing and porcelain — from init to branching:

```bash
# 1. Initialize
$ ./mygit init
Initialized empty mygit repository in ./.mygit/

# 2. Configure identity
$ ./mygit config user.name "Jelsin"
$ ./mygit config user.email "jelsin@local"

# 3. Create files, stage, and commit
$ echo "hello" > hello.txt
$ echo "int main() { return 0; }" > main.c
$ ./mygit add hello.txt main.c
$ ./mygit commit -m "initial commit"

# 4. Make changes and commit again
$ echo "goodbye" >> hello.txt
$ ./mygit add hello.txt
$ ./mygit status
$ ./mygit commit -m "update hello"

# 5. Create a branch and switch to it
$ ./mygit branch feature
$ ./mygit checkout feature

# 6. Walk the history
$ ./mygit log HEAD
```

## Architecture

```
src/
├── main.c            CLI entry point — 15 subcommands dispatched here
├── repo.c            Repository initialization (mygit init)
├── hash.c            SHA-1 hashing + hex conversion utilities
├── object.c          Generic object store — compress/write/read from .mygit/objects/
├── blob.c            Blob creation (file → "blob <size>\0<content>" → hash → store)
├── tree.c            Tree creation (sorted entries → binary format → hash → store)
├── commit.c          Commit creation (tree + parents + metadata → hash → store)
├── ref.c             Ref resolution (symbolic + direct), update, list, HEAD
├── config.c          INI-style config parser (get/set/init)
├── index.c           Staging area — DIRC v2 binary format, write-tree
├── tui.c             ncurses init, event loop, view dispatch, KEY_RESIZE handling
├── view_dashboard.c  ASCII header, stats panel, menu navigation
├── view_objects.c    Object explorer — scans .mygit/objects/, type detection
├── view_detail.c     Object inspection — blob content, tree entries, commit metadata
├── view_tree.c       Tree browser — subtree navigation with stack-based back
├── view_commits.c    Commit log — sorted timeline with detail inspection
├── view_hashfile.c   File browser — hash files as blobs
├── view_commitform.c Commit form — stage and commit from TUI
├── view_branches.c   Branch manager — create, switch, delete branches
└── view_files.c      File browser — navigate working tree

include/
├── repo.h       mygit_init()
├── hash.h       mygit_sha1(), mygit_hex()
├── object.h     mygit_object_write(), mygit_object_read()
├── blob.h       mygit_blob_hash(), mygit_blob_write(), mygit_blob_read()
├── tree.h       mygit_tree_hash(), mygit_tree_write(), mygit_tree_read()
├── commit.h     mygit_commit_write(), mygit_commit_read()
├── ref.h        mygit_ref_resolve(), mygit_ref_update(), mygit_ref_list()
├── config.h     mygit_config_get(), mygit_config_set(), mygit_config_init()
├── index.h      mygit_index_add(), mygit_index_write(), mygit_write_tree()
└── tui.h        TUI state, view enum, color pairs, view function prototypes

tests/
├── test_repo.c     10 tests — init, reinit, structure, HEAD content
├── test_blob.c     12 tests — SHA-1, hash, write, read, known SHA matching
├── test_tree.c     12 tests — entry sorting, nested trees, binary format
├── test_commit.c    9 tests — root/parent commits, DAG walking, error cases
├── test_config.c   15 tests — get/set, case insensitive, roundtrip, init
├── test_ref.c      25 tests — resolve, symbolic, circular, update, list, HEAD
└── test_index.c    13 tests — add, sort, remove, binary roundtrip, write-tree
```

## How git objects work (what I learned)

### Everything is content-addressed

Git doesn't store files by name or path — it stores them by the SHA-1 hash of their content. Two files with the same content produce the same hash and are stored only once. This is why git is so space-efficient.

### The three object types

**Blob** — raw file content, wrapped with a header:
```
blob <size>\0<content>
```
The entire thing (header + content) is SHA-1 hashed and zlib-compressed before writing to disk.

**Tree** — a directory snapshot. Each entry is binary-encoded:
```
<mode> <name>\0<20-byte-SHA-1>
```
Entries are sorted by name (with a special rule: tree names sort as if they end with `/`). The whole thing gets the same `tree <size>\0` header treatment as blobs.

**Commit** — points to a tree (the snapshot) and optionally to parent commits (the history):
```
tree <40-hex-sha>
parent <40-hex-sha>       (zero or more)
author <name> <email> <timestamp> <tz>
committer <name> <email> <timestamp> <tz>

<message>
```

### The DAG

Commits form a Directed Acyclic Graph. Each commit points to its parent(s), creating a chain. `git log` walks this chain. Branches are just pointers to specific commits. Merges create commits with multiple parents.

### The object store

Objects live in `.mygit/objects/` using a two-character prefix sharding:
```
.mygit/objects/95/d09f2b10159347eece71399a7e2e907ea3df4f
               ^^
            first two hex chars of SHA-1
```

Every object is zlib-compressed on disk. This keeps the repository small.

### Refs — just files pointing to commits

A branch is a file containing a 40-char SHA-1 hex string. `refs/heads/master` holds the SHA of the latest commit on master. HEAD is a symbolic ref: `ref: refs/heads/master\n`. That's it — there's no magic. Switching branches means updating HEAD to point to a different ref.

Symbolic refs can chain: HEAD → refs/heads/master → a commit SHA. Resolution follows the chain up to a depth limit (to detect circular refs).

### The index — the staging area

The index (`.mygit/index`) is a binary file in DIRC v2 format. Each entry has stat metadata (ctime, mtime, dev, ino, mode, uid, gid, size), a 20-byte SHA-1, flags, and a NUL-terminated path. Entries are 8-byte aligned with NUL padding. The entire file ends with a SHA-1 checksum of itself.

`git add` writes to the index. `git commit` reads the index, builds a tree from it (recursively creating nested trees for subdirectories), and creates a commit pointing to that tree.

### Config — INI-style with sections

Git's config is a simple INI file with `[section]` headers and `key = value` pairs. Section names are case-insensitive, keys are case-insensitive, values preserve case. There's no complex nested structure — just flat sections.

## Non-obvious gotchas discovered

- **Tree entry sorting**: git sorts tree entries as if directory names end with `/`. So `foo` (a file) sorts differently than `foo` (a directory). This matters for hash reproducibility.
- **Blob header is part of the hash**: The SHA-1 is computed over `"blob <size>\0<content>"`, not just the content. If you hash only the content, you'll get a different SHA than git.
- **Zlib compression uses `Z_FINISH` in one shot**: For small objects, git compresses in a single `deflate()` call with `Z_FINISH`. No streaming needed.
- **Tree entries use raw 20-byte hashes**: Unlike commits (which use 40-char hex), tree entries store the hash as raw binary. This tripped me up when parsing.
- **Commit timestamps are Unix epoch integers**: Stored as plain decimal integers followed by a timezone offset like `+0000`.
- **Index entries are 8-byte aligned**: Each DIRC v2 entry is padded with NUL bytes to the next 8-byte boundary. Miss this and the whole binary format breaks.
- **The index checksum covers itself**: The last 20 bytes of the index file are a SHA-1 over everything before them. This makes corruption detection trivial.
- **Symbolic ref resolution needs a depth limit**: Without it, `HEAD → refs/heads/a → HEAD` creates an infinite loop. Real git limits resolution depth.
- **`_POSIX_C_SOURCE 200809L` needed for `st_mtim`**: The `stat` struct's nanosecond timestamp fields aren't available without this define on Linux.
- **`snprintf` with `-Werror` and `-Wformat-truncation`**: GCC is strict — if the buffer *could* truncate, it's an error. Must size buffers precisely.

## Tests

96 tests across 7 suites, all passing:

```bash
$ make test
=== Repo init tests ===          10/10 passed
=== Blob tests ===               12/12 passed
=== Tree tests ===               12/12 passed
=== Commit tests ===              9/9 passed
=== Config tests ===             15/15 passed
=== Ref tests ===                25/25 passed
=== Index tests ===              13/13 passed
```

Tests verify SHA-1 hash correctness against known values, round-trip write/read integrity for all object types, binary format parsing, symbolic ref resolution chains, circular ref detection, INI config parsing, DIRC v2 index format, and DAG traversal.

## Language

C17 — compiled with `-Wall -Wextra -Werror -pedantic`. Zero warnings.

## Dependencies

- **zlib** — object compression/decompression
- **OpenSSL** (`libcrypto`) — SHA-1 hashing
- **ncurses** (`ncursesw`) — interactive TUI with wide-character/UTF-8 support

No git libraries. No VCS libraries. Everything is built from scratch.
