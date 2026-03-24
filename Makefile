CC      = gcc
CFLAGS  = -std=c17 -Wall -Wextra -Werror -pedantic -Iinclude -g
LDFLAGS =

SRC     = src/main.c src/repo.c
OBJ     = $(SRC:.c=.o)
TARGET  = mygit

TEST_SRC    = tests/test_repo.c src/repo.c
TEST_TARGET = tests/test_repo

.PHONY: all clean test debug

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OBJ) $(TARGET) $(TEST_TARGET)

debug: CFLAGS += -fsanitize=address -fsanitize=undefined
debug: clean all
