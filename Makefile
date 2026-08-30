CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99 -D_POSIX_C_SOURCE=200809L
LIBS = -lcurl -lsqlite3

SRCS = minijson.c model_adapter.c c_agent.c c_harness.c linenoise.c main.c
OBJS = $(SRCS:.c=.o)
TARGET = c_agent_system

TEST_SRCS = minijson.c model_adapter.c c_agent.c c_harness.c linenoise.c test_suite.c
TEST_TARGET = test_runner

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_SRCS)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRCS) $(LIBS)

clean:
	rm -f $(OBJS) $(TARGET) $(TEST_TARGET) c_agent_memory.sqlite test_agent_memory.sqlite test_harness_mem.sqlite test_sample.txt

.PHONY: all test clean
