# Build Your Own Git

A minimal git implementation written from scratch in C. No libraries, no shortcuts — just raw content-addressed storage, SHA-1 hashing, zlib compression, and the DAG structure that makes git work.

## Why

To understand what happens under the hood when you run `git add`, `git commit`, or `git log`. This project implements the plumbing layer — the low-level commands that the porcelain (user-facing) commands are built on top of.

## Building

### Prerequisites

- GCC with C17 support
- zlib (`libz-dev` / `zlib-devel`)
- OpenSSL (`libssl-dev` / `openssl-devel`)
- ncurses (`libncurses-dev` / `ncurses-devel`) — for the TUI

### Compile

```bash
make          # builds the mygit binary
make test     # runs all 43 tests (repo, blob, tree, commit)
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

## Complete workflow example

Here's a full session showing how the pieces connect — this is essentially what `git add` + `git commit` do under the hood:

```bash
# 1. Initialize
$ ./mygit init
Initialized empty mygit repository in ./.mygit/

# 2. Create some files and store them as blobs
$ echo "hello" > hello.txt
$ echo "int main() { return 0; }" > main.c

$ ./mygit hash-object -w hello.txt
# => ce013625030ba8dba906f756967f9e9ca394464a

$ ./mygit hash-object -w main.c
# => <sha of main.c>

# 3. Trees reference blobs — created programmatically via the C API
#    (no write-tree CLI command yet — trees are built in tests and code)

# 4. Commits reference trees
$ ./mygit commit-tree <tree-sha> -m "initial commit"
# => <commit-sha>

# 5. Chain commits with parents
$ ./mygit commit-tree <new-tree-sha> -p <commit-sha> -m "second commit"
# => <commit2-sha>

# 6. Walk the history
$ ./mygit log <commit2-sha>
# Shows both commits, following the DAG
```

## Architecture

```
src/
├── main.c            CLI entry point — dispatches to subcommands
├── repo.c            Repository initialization (mygit init)
├── hash.c            SHA-1 hashing + hex conversion utilities
├── object.c          Generic object store — compress/write/read from .mygit/objects/
├── blob.c            Blob creation (file → "blob <size>\0<content>" → hash → store)
├── tree.c            Tree creation (sorted entries → binary format → hash → store)
├── commit.c          Commit creation (tree + parents + metadata → hash → store)
├── tui.c             ncurses init, event loop, view dispatch, KEY_RESIZE handling
├── view_dashboard.c  ASCII header, stats panel, menu navigation
├── view_objects.c    Object explorer — scans .mygit/objects/, type detection
├── view_detail.c     Object inspection — blob content, tree entries, commit metadata
├── view_tree.c       Tree browser — subtree navigation with stack-based back
├── view_commits.c    Commit log — sorted timeline with detail inspection
└── view_hashfile.c   File browser — hash files as blobs

include/
├── repo.h       mygit_init()
├── hash.h       mygit_sha1(), mygit_hex()
├── object.h     mygit_object_write(), mygit_object_read()
├── blob.h       mygit_blob_hash(), mygit_blob_write(), mygit_blob_read()
├── tree.h       mygit_tree_hash(), mygit_tree_write(), mygit_tree_read()
├── commit.h     mygit_commit_write(), mygit_commit_read()
└── tui.h        TUI state, view enum, color pairs, view function prototypes

tests/
├── test_repo.c     10 tests — init, reinit, structure, HEAD content
├── test_blob.c     12 tests — hash, write, read, known SHA matching
├── test_tree.c     12 tests — entry sorting, nested trees, binary format
└── test_commit.c    9 tests — root/parent commits, DAG walking, error cases
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

## Non-obvious gotchas discovered

- **Tree entry sorting**: git sorts tree entries as if directory names end with `/`. So `foo` (a file) sorts differently than `foo` (a directory). This matters for hash reproducibility.
- **Blob header is part of the hash**: The SHA-1 is computed over `"blob <size>\0<content>"`, not just the content. If you hash only the content, you'll get a different SHA than git.
- **Zlib compression uses `Z_FINISH` in one shot**: For small objects, git compresses in a single `deflate()` call with `Z_FINISH`. No streaming needed.
- **Tree entries use raw 20-byte hashes**: Unlike commits (which use 40-char hex), tree entries store the hash as raw binary. This tripped me up when parsing.
- **Commit timestamps are Unix epoch integers**: Stored as plain decimal integers followed by a timezone offset like `+0000`.

## Tests

43 tests across 4 suites, all passing:

```bash
$ make test
=== Repo init tests ===          10/10 passed
=== Blob tests ===               12/12 passed
=== Tree tests ===               12/12 passed
=== Commit tests ===              9/9 passed
```

Tests verify hash correctness against known SHA-1 values, round-trip write/read integrity, error handling, and DAG traversal.

## Language

C17 — compiled with `-Wall -Wextra -Werror -pedantic`. Zero warnings.

## Dependencies

- **zlib** — object compression/decompression
- **OpenSSL** (`libcrypto`) — SHA-1 hashing
- **ncurses** (`ncursesw`) — interactive TUI with wide-character/UTF-8 support

No git libraries. No VCS libraries. Everything is built from scratch.
