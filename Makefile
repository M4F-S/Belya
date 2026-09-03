CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99 -D_POSIX_C_SOURCE=200809L
LIBS = -lcurl -lsqlite3

SRCS = linenoise.c minijson.c mcp_client.c model_adapter.c belya_agent.c belya_harness.c telegram_adapter.c main.c
OBJS = $(SRCS:.c=.o)
TARGET = belya

TEST_SRCS = linenoise.c minijson.c mcp_client.c model_adapter.c belya_agent.c belya_harness.c telegram_adapter.c test_suite.c
TEST_TARGET = belya_test

BENCHMARK_SRCS = linenoise.c minijson.c mcp_client.c model_adapter.c belya_agent.c belya_harness.c telegram_adapter.c benchmark_runner.c
BENCHMARK_TARGET = belya_benchmark

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_SRCS)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRCS) $(LIBS)

benchmark: $(BENCHMARK_TARGET) $(TARGET)
	./$(BENCHMARK_TARGET)

$(BENCHMARK_TARGET): $(BENCHMARK_SRCS)
	$(CC) $(CFLAGS) -o $@ $(BENCHMARK_SRCS) $(LIBS)

clean:
	rm -f $(OBJS) $(TARGET) $(TEST_TARGET) $(BENCHMARK_TARGET) belya_memory.sqlite test_*.sqlite* test_sample.txt bench_mem.sqlite* .belya_history 

.PHONY: all test benchmark clean

