CC = gcc
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    DEAD_STRIP = -Wl,-dead_strip
else
    DEAD_STRIP = -Wl,--gc-sections
endif
CFLAGS = -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -O2 -ffunction-sections -fdata-sections -std=c99 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700
LIBS = $(DEAD_STRIP) -lcurl -lsqlite3

SRCS = linenoise.c minijson.c minifrontmatter.c mcp_client.c mcp_http.c jev_client.c model_adapter.c belya_agent.c belya_harness.c permissions.c telegram_render.c telegram_adapter.c belya_channel.c main.c
OBJS = $(SRCS:.c=.o)
BUILD_DIR = build
TARGET = $(BUILD_DIR)/belya
DEPLOY_BIN = belya
DEPLOY_SVC = belya
DEPLOY_RESTART = systemctl restart $(DEPLOY_SVC)

TEST_SRCS = linenoise.c minijson.c minifrontmatter.c mcp_client.c mcp_http.c jev_client.c model_adapter.c belya_agent.c belya_harness.c permissions.c telegram_render.c telegram_adapter.c belya_channel.c test_suite.c
TEST_TARGET = belya_test
LOOP_SRCS = $(filter-out test_suite.c,$(TEST_SRCS)) tests/test_agent_loop.c
LOOP_TARGET = build/test_agent_loop
PERMS_SRCS = $(filter-out test_suite.c,$(TEST_SRCS)) tests/test_permissions.c
PERMS_TARGET = build/test_permissions
MCP_HTTP_TEST_TARGET = build/test_mcp_http

BENCHMARK_SRCS = linenoise.c minijson.c minifrontmatter.c mcp_client.c mcp_http.c jev_client.c model_adapter.c belya_agent.c belya_harness.c permissions.c telegram_render.c telegram_adapter.c belya_channel.c benchmark_runner.c
BENCHMARK_TARGET = belya_benchmark

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_TARGET) $(MCP_HTTP_TEST_TARGET)
	./$(TEST_TARGET)
	./$(MCP_HTTP_TEST_TARGET)

$(MCP_HTTP_TEST_TARGET): mcp_http_selftest.c mcp_http.c mcp_client.c minijson.c mcp_http.h mcp_client.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ mcp_http_selftest.c mcp_http.c mcp_client.c minijson.c $(LIBS)

$(TEST_TARGET): $(TEST_SRCS)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRCS) $(LIBS)

# Authorisation table tests: permission_classify() is pure; the gate is checked
# against a real harness in both permission modes.
test-perms: $(PERMS_TARGET)
	./$(PERMS_TARGET)

$(PERMS_TARGET): $(PERMS_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I. -o $@ $(PERMS_SRCS) $(LIBS)

# First real loop test: drives belya_agent_step() against a scripted model.
test-mock:
	python3 tests/test_mock_server.py

# Telegram markdown -> HTML renderer (pure function).
test-tgrender:
	$(CC) -Wall -Wextra -Werror -O1 -std=c99 -o build/test_telegram_render tests/test_telegram_render.c telegram_render.c
	./build/test_telegram_render

# Telegram send path against a local fake Bot API (asserts the wire payload).
TGSEND_SRCS = $(filter-out test_suite.c,$(TEST_SRCS)) tests/test_telegram_send.c
TGSEND_TARGET = build/test_telegram_send

test-tgsend: $(TGSEND_TARGET)
	@sh tests/run_tgsend.sh

$(TGSEND_TARGET): $(TGSEND_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I. -o $@ $(TGSEND_SRCS) $(LIBS)

# Exercises every registered tool through the real binary using a scripted model.
test-tools: $(TARGET)
	@BELYA_BIN=./$(TARGET) python3 tests/tool_probe.py ./$(TARGET) bash read_file write_file list_dir search_files git_status

test-loop: $(LOOP_TARGET)
	@python3 tests/mock_model_server.py --port 18081 --script tests/loop_script.json & \
	  pid=$$!; sleep 0.8; \
	  MODEL_MOCK_URL=http://127.0.0.1:18081/v1/chat/completions ./$(LOOP_TARGET); \
	  rc=$$?; kill $$pid 2>/dev/null; wait $$pid 2>/dev/null; exit $$rc

$(LOOP_TARGET): $(LOOP_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I. -o $@ $(LOOP_SRCS) $(LIBS)

benchmark: $(BENCHMARK_TARGET) $(TARGET)
	./$(BENCHMARK_TARGET)

arena: benchmark

$(BENCHMARK_TARGET): $(BENCHMARK_SRCS)
	$(CC) $(CFLAGS) -o $@ $(BENCHMARK_SRCS) $(LIBS)

# deploy: publish the built artifact to the path the service actually runs, respecting the
# immutable flag, and restart ONLY when the content really changed (H3/H4).
deploy: $(TARGET)
	@set -e; \
	if [ -e "$(DEPLOY_BIN)" ] && cmp -s "$(TARGET)" "$(DEPLOY_BIN)"; then \
	  echo "[deploy] $(DEPLOY_BIN) already matches $(TARGET) - no restart needed"; \
	else \
	  if [ -e "$(DEPLOY_BIN)" ] && ! chattr -i "$(DEPLOY_BIN)" 2>/dev/null; then \
	    echo "[deploy] REFUSED: cannot clear the immutable flag on $(DEPLOY_BIN)." >&2; \
	    echo "[deploy] Publishing is an OPERATOR action: the service runs without" >&2; \
	    echo "[deploy] CAP_LINUX_IMMUTABLE, so an agent cannot swap the running binary." >&2; \
	    echo "[deploy] Nothing was changed. Run make deploy as root outside the service." >&2; \
	    exit 1; \
	  fi; \
	  echo "[deploy] installing $(TARGET) -> $(DEPLOY_BIN)"; \
	  if [ -e "$(DEPLOY_BIN)" ]; then cp -a "$(DEPLOY_BIN)" "$(DEPLOY_BIN).lkg"; fi; \
	  install -m 0755 "$(TARGET)" "$(DEPLOY_BIN).new"; \
	  mv -f "$(DEPLOY_BIN).new" "$(DEPLOY_BIN)"; \
	  chattr +i "$(DEPLOY_BIN)" 2>/dev/null || true; \
	  $(DEPLOY_RESTART); \
	fi

# release: the whole ritual in one command - clean build, test gate, then deploy (H4).
release:
	$(MAKE) clean
	$(MAKE) $(TARGET)
	$(MAKE) test
	$(MAKE) deploy

clean:
	# NOTE: the deployed artifact is chattr +i and is NOT build output - never rm it here.
	-rm -rf $(BUILD_DIR)
	-rm -f $(OBJS) $(TEST_TARGET) $(BENCHMARK_TARGET) test_*.sqlite* test_sample.txt bench_mem.sqlite* .belya_history

gdrive:
	@./scripts/sync_to_gdrive.sh

.PHONY: all test benchmark arena clean gdrive deploy release

# drift-guard
drift:
	python3 tools/drift_check.py

prd-lock:
	python3 tools/drift_check.py --update-lock
