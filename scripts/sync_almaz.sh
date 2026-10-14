#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BELYA_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ALMAZ_DIR="${1:-$BELYA_DIR/../Almaz}"

if [ ! -d "$ALMAZ_DIR" ]; then
    echo "[-] Error: Almaz directory not found at $ALMAZ_DIR" >&2
    exit 1
fi

echo "[*] Syncing shared pure-infrastructure headers & scripts from Belya -> Almaz ($ALMAZ_DIR)..."

# Safe pure-infrastructure files that are 100% identical between Belya and Almaz.
# NEVER overwrite Almaz's divergent organism core (belya_agent.*, belya_harness.*,
# model_adapter.*, telegram_adapter.*, main.c, test_suite.c, benchmark_runner.c, Makefile) with sed!
SHARED_FILES=(
    "belya_common.h"
    "common.h"
    "linenoise.c"
    "linenoise.h"
    "minijson.c"
    "minijson.h"
    "minifrontmatter.c"
    "minifrontmatter.h"
    "mcp_client.c"
    "mcp_client.h"
    "jev_client.c"
    "jev_client.h"
    "belya_channel.h"
    "belya_channel.c"
)

for f in "${SHARED_FILES[@]}"; do
    if [ -f "$BELYA_DIR/$f" ]; then
        cp "$BELYA_DIR/$f" "$ALMAZ_DIR/$f"
    fi
done

mkdir -p "$ALMAZ_DIR/scripts"
cp "$BELYA_DIR/scripts/verify_phase.sh" "$ALMAZ_DIR/scripts/verify_phase.sh"
cp "$BELYA_DIR/scripts/sync_to_gdrive.sh" "$ALMAZ_DIR/scripts/sync_to_gdrive.sh"
chmod +x "$ALMAZ_DIR/scripts/verify_phase.sh" "$ALMAZ_DIR/scripts/sync_to_gdrive.sh"

echo "[+] Shared infrastructure synchronized safely to Almaz without clobbering organism modules."
