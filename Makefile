CC      = gcc
CFLAGS  = -std=c17 -Wall -Wextra -Werror -pedantic -Iinclude -g
LDFLAGS = -lz -lcrypto

SRC     = src/main.c src/repo.c src/hash.c src/object.c src/blob.c
OBJ     = $(SRC:.c=.o)
TARGET  = mygit

TEST_REPO_SRC    = tests/test_repo.c src/repo.c
TEST_REPO_TARGET = tests/test_repo

TEST_BLOB_SRC    = tests/test_blob.c src/repo.c src/hash.c src/object.c src/blob.c
TEST_BLOB_TARGET = tests/test_blob

.PHONY: all clean test test-repo test-blob debug

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: test-repo test-blob

test-repo: $(TEST_REPO_TARGET)
	./$(TEST_REPO_TARGET)

test-blob: $(TEST_BLOB_TARGET)
	./$(TEST_BLOB_TARGET)

$(TEST_REPO_TARGET): $(TEST_REPO_SRC)
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_BLOB_TARGET): $(TEST_BLOB_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJ) $(TARGET) $(TEST_REPO_TARGET) $(TEST_BLOB_TARGET)

debug: CFLAGS += -fsanitize=address -fsanitize=undefined
debug: LDFLAGS += -fsanitize=address -fsanitize=undefined
debug: clean all
