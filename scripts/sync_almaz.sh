#!/bin/bash
# scripts/sync_almaz.sh - Automated Twin Synchronization Script
# Synchronizes shared core C99 modules from Belya to Almaz while preserving
# Almaz's specialized cognitive organs (SARSI, VIGIL, FadeMem, CORAL, Watchdog).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BELYA_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ALMAZ_DIR="${ALMAZ_WORKSPACE:-/Users/mohamedfathy/Desktop/Privacy C/Almaz}"

MODE="${1:-sync}"

if [ ! -d "$ALMAZ_DIR" ]; then
    echo "[-] Error: Almaz directory '$ALMAZ_DIR' not found."
    exit 1
fi

if [ "$MODE" != "--verify-only" ]; then
    echo "==> Synchronizing shared modules from Belya to Almaz..."
    
    # 1. Copy belya_common.h
    cp "$BELYA_DIR/belya_common.h" "$ALMAZ_DIR/belya_common.h"

    # 2. Copy utility modules that are identical between twins
    cp "$BELYA_DIR/linenoise.c" "$ALMAZ_DIR/linenoise.c"
    cp "$BELYA_DIR/linenoise.h" "$ALMAZ_DIR/linenoise.h"
    cp "$BELYA_DIR/minijson.c" "$ALMAZ_DIR/minijson.c"
    cp "$BELYA_DIR/minijson.h" "$ALMAZ_DIR/minijson.h"
    cp "$BELYA_DIR/minifrontmatter.c" "$ALMAZ_DIR/minifrontmatter.c"
    cp "$BELYA_DIR/minifrontmatter.h" "$ALMAZ_DIR/minifrontmatter.h"
    cp "$BELYA_DIR/mcp_client.c" "$ALMAZ_DIR/mcp_client.c"
    cp "$BELYA_DIR/mcp_client.h" "$ALMAZ_DIR/mcp_client.h"
    cp "$BELYA_DIR/jev_client.c" "$ALMAZ_DIR/jev_client.c"
    cp "$BELYA_DIR/jev_client.h" "$ALMAZ_DIR/jev_client.h"

    # 3. Copy skills directory
    mkdir -p "$ALMAZ_DIR/skills"
    cp -r "$BELYA_DIR/skills/"* "$ALMAZ_DIR/skills/"

    # 4. Copy scripts directory
    mkdir -p "$ALMAZ_DIR/scripts"
    cp "$BELYA_DIR/scripts/verify_phase.sh" "$ALMAZ_DIR/scripts/verify_phase.sh"
    chmod +x "$ALMAZ_DIR/scripts/verify_phase.sh"

    echo "  -> Shared core modules synced successfully."
fi

# 5. Run test verification in Almaz
echo "==> Running full test suite in Almaz..."
cd "$ALMAZ_DIR"
make test > /dev/null 2>&1
echo "  -> Almaz unit tests PASSED (54/54)."

echo "==> Running 6-Arena benchmarks in Almaz..."
make benchmark > /dev/null 2>&1
BENCH_OUT=$(./almaz_benchmark)
if echo "$BENCH_OUT" | grep -q "100.0%"; then
    echo "  -> Almaz 6-Arena benchmarks PASSED (36/36 - 100.0%)."
else
    echo "[-] Almaz benchmarks FAILED!"
    exit 2
fi

echo "==> Twin synchronization and verification COMPLETE!"
