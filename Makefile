CC      = gcc
CFLAGS  = -std=c17 -Wall -Wextra -Werror -pedantic -Iinclude -g
LDFLAGS = -lz -lcrypto -lncursesw

SRC     = src/main.c src/repo.c src/hash.c src/object.c src/blob.c src/tree.c src/commit.c src/tui.c src/view_dashboard.c
OBJ     = $(SRC:.c=.o)
TARGET  = mygit

TEST_REPO_SRC    = tests/test_repo.c src/repo.c
TEST_REPO_TARGET = tests/test_repo

TEST_BLOB_SRC    = tests/test_blob.c src/repo.c src/hash.c src/object.c src/blob.c
TEST_BLOB_TARGET = tests/test_blob

TEST_TREE_SRC    = tests/test_tree.c src/repo.c src/hash.c src/object.c src/blob.c src/tree.c
TEST_TREE_TARGET = tests/test_tree

TEST_COMMIT_SRC    = tests/test_commit.c src/repo.c src/hash.c src/object.c src/blob.c src/tree.c src/commit.c
TEST_COMMIT_TARGET = tests/test_commit

.PHONY: all clean test test-repo test-blob test-tree test-commit debug

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: test-repo test-blob test-tree test-commit

test-repo: $(TEST_REPO_TARGET)
	./$(TEST_REPO_TARGET)

test-blob: $(TEST_BLOB_TARGET)
	./$(TEST_BLOB_TARGET)

$(TEST_REPO_TARGET): $(TEST_REPO_SRC)
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_BLOB_TARGET): $(TEST_BLOB_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test-tree: $(TEST_TREE_TARGET)
	./$(TEST_TREE_TARGET)

$(TEST_TREE_TARGET): $(TEST_TREE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test-commit: $(TEST_COMMIT_TARGET)
	./$(TEST_COMMIT_TARGET)

$(TEST_COMMIT_TARGET): $(TEST_COMMIT_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJ) $(TARGET) $(TEST_REPO_TARGET) $(TEST_BLOB_TARGET) $(TEST_TREE_TARGET) $(TEST_COMMIT_TARGET)

debug: CFLAGS += -fsanitize=address -fsanitize=undefined
debug: LDFLAGS += -fsanitize=address -fsanitize=undefined
debug: clean all
