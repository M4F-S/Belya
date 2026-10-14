#!/usr/bin/env bash
set -euo pipefail

PHASE_NAME="${1:-Phase_Verification}"
echo "=================================================================="
echo "🔒 7-Gate Verification Pipeline — Target: ${PHASE_NAME}"
echo "=================================================================="

if [ -f "watchdog.c" ]; then
    BIN="almaz"
    TEST_BIN="almaz_test"
    BENCH_BIN="almaz_benchmark"
    if gcc -Oz -E -x c /dev/null >/dev/null 2>&1; then
        OPT_FLAG="-Oz -ffunction-sections -fdata-sections"
    else
        OPT_FLAG="-Os -ffunction-sections -fdata-sections"
    fi
    EXTRA_LIBS="-lm"
    EXTRA_SRCS="timeline_anomaly.c budget_store.c health_watcher.c"
else
    BIN="belya"
    TEST_BIN="belya_test"
    BENCH_BIN="belya_benchmark"
    OPT_FLAG="-O2 -ffunction-sections -fdata-sections"
    EXTRA_LIBS=""
    EXTRA_SRCS=""
fi

STRICT_FLAGS="-std=c99 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2"

# Gate 1: Clean Release Compile with -Werror & Strict Flags
echo "[Gate 1/7] Compiling release binary with strict C99 + POSIX flags..."
make clean >/dev/null
make CFLAGS="$STRICT_FLAGS $OPT_FLAG"

# Gate 2: Unit Test Suite
echo "[Gate 2/7] Building and running unit test suite..."
make test CFLAGS="$STRICT_FLAGS $OPT_FLAG"

# Gate 3: Holdout Regression Suite (if present)
if [ -f "test_holdout.c" ]; then
    echo "[Gate 3/7] Building and running holdout regression suite..."
    make holdout CFLAGS="$STRICT_FLAGS $OPT_FLAG"
else
    echo "[Gate 3/7] Holdout suite N/A for Belya — skipped."
fi

# Gate 4: AddressSanitizer + UndefinedBehaviorSanitizer Clean Pass
echo "[Gate 4/7] Compiling and running under ASan + UBSan..."
gcc $STRICT_FLAGS -O1 -g -fsanitize=address,undefined \
    $EXTRA_SRCS linenoise.c minijson.c minifrontmatter.c mcp_client.c jev_client.c \
    model_adapter.c belya_agent.c belya_harness.c telegram_adapter.c \
    belya_channel.c test_suite.c -o "${TEST_BIN}_asan" -lcurl -lsqlite3 $EXTRA_LIBS
ASAN_OPTIONS="detect_leaks=0" ./"${TEST_BIN}_asan" >/dev/null
rm -f "${TEST_BIN}_asan"
rm -rf "${TEST_BIN}_asan.dSYM"

# Gate 5: Binary Size Invariant (< 350 KB stripped)
echo "[Gate 5/7] Verifying stripped binary size (< 350,000 bytes)..."
cp "$BIN" "${BIN}_stripped"
strip -x "${BIN}_stripped" 2>/dev/null || strip "${BIN}_stripped"
BIN_SIZE=$(wc -c < "${BIN}_stripped" | tr -d ' ')
rm -f "${BIN}_stripped"
echo "    Stripped ${BIN} size: ${BIN_SIZE} bytes (Limit: 350000 bytes)"
if [ "$BIN_SIZE" -ge 350000 ]; then
    echo "❌ FAIL: Stripped binary size (${BIN_SIZE} B) exceeds 350 KB limit!"
    exit 1
fi

# Gate 6: Full Benchmark Arena Verification
echo "[Gate 6/7] Running 6-Arena Competitive Benchmark..."
make benchmark CFLAGS="$STRICT_FLAGS $OPT_FLAG"

# Gate 7: Hygiene Check (Zero untracked SQLite WAL/SHM or temp artifacts)
echo "[Gate 7/7] Cleaning temporary test artifacts..."
rm -f test_*.sqlite* test_sample.txt bench_mem.sqlite*

echo "=================================================================="
echo "✅ ALL 7 GATES PASSED FOR: ${PHASE_NAME}"
echo "=================================================================="
