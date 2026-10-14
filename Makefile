CC = gcc
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    DEAD_STRIP = -Wl,-dead_strip
else
    DEAD_STRIP = -Wl,--gc-sections
endif
CFLAGS = -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -O2 -ffunction-sections -fdata-sections -std=c99 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700
LIBS = $(DEAD_STRIP) -lcurl -lsqlite3

SRCS = linenoise.c minijson.c minifrontmatter.c mcp_client.c jev_client.c model_adapter.c belya_agent.c belya_harness.c telegram_adapter.c belya_channel.c main.c
OBJS = $(SRCS:.c=.o)
TARGET = belya

TEST_SRCS = linenoise.c minijson.c minifrontmatter.c mcp_client.c jev_client.c model_adapter.c belya_agent.c belya_harness.c telegram_adapter.c belya_channel.c test_suite.c
TEST_TARGET = belya_test

BENCHMARK_SRCS = linenoise.c minijson.c minifrontmatter.c mcp_client.c jev_client.c model_adapter.c belya_agent.c belya_harness.c telegram_adapter.c belya_channel.c benchmark_runner.c
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
	rm -f $(OBJS) $(TARGET) $(TEST_TARGET) $(BENCHMARK_TARGET) test_*.sqlite* test_sample.txt bench_mem.sqlite* .belya_history 

gdrive:
	@./scripts/sync_to_gdrive.sh

.PHONY: all test benchmark clean gdrive
