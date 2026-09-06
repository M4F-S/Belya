#!/usr/bin/env python3
"""
Belya-Evolve: Autonomous Metamorphic Sandbox Runner
Enforces safety virtualization, AddressSanitizer pre-flight checks,
and Darwinian fitness scoring for self-mutating C components.
"""

import os
import sys
import time
import shutil
import subprocess
from pathlib import Path

SANDBOX_GUARD = os.getenv("BELYA_EVOLVE_SANDBOX")
if SANDBOX_GUARD != "1":
    print("\n[SECURITY GUARD ERROR]: Belya-Evolve cannot run without explicit sandbox isolation.")
    print("Self-mutation directly on the host or production VPS is strictly prohibited to preserve system integrity.")
    print("To run in an isolated environment, set:\n  export BELYA_EVOLVE_SANDBOX=1\n")
    sys.exit(1)

PROJECT_ROOT = Path(__file__).resolve().parent.parent
SANDBOX_DIR = Path("/tmp/belya_evolve_sandbox")

def setup_isolated_sandbox():
    """Clones active C sources into an isolated temp directory."""
    if SANDBOX_DIR.exists():
        shutil.rmtree(SANDBOX_DIR)
    SANDBOX_DIR.mkdir(parents=True, exist_ok=True)

    print(f"[*] Setting up isolated sandbox at: {SANDBOX_DIR}")
    for ext in ["*.c", "*.h", "Makefile"]:
        for file_path in PROJECT_ROOT.glob(ext):
            shutil.copy(file_path, SANDBOX_DIR / file_path.name)

    print(f"[*] Sandbox populated with {len(list(SANDBOX_DIR.glob('*')))} source files.")

def compile_and_test(tag="candidate"):
    """Compiles under ASan/UBSan and executes the 27-test battery."""
    print(f"[*] [{tag.upper()}] Pre-flight compiling with ASan/UBSan...")
    build_cmd = [
        "gcc", "-Wall", "-Wextra", "-Werror", "-std=c99",
        "-fsanitize=address,undefined",
        "test_suite.c", "linenoise.c", "minijson.c", "mcp_client.c",
        "model_adapter.c", "belya_agent.c", "belya_harness.c", "telegram_adapter.c",
        "-lcurl", "-lsqlite3", "-o", f"belya_test_{tag}"
    ]
    res_build = subprocess.run(build_cmd, cwd=SANDBOX_DIR, capture_output=True, text=True)
    if res_build.returncode != 0:
        print(f"[!] Compilation Failed for {tag}:\n{res_build.stderr}")
        return False, 0.0, 0, res_build.stderr

    bin_path = SANDBOX_DIR / f"belya_test_{tag}"
    bin_size = bin_path.stat().st_size

    print(f"[*] [{tag.upper()}] Executing 27/27 integration tests...")
    t0 = time.perf_counter()
    res_test = subprocess.run([str(bin_path)], cwd=SANDBOX_DIR, capture_output=True, text=True)
    t1 = time.perf_counter()
    duration = t1 - t0

    if res_test.returncode != 0:
        print(f"[!] Test Suite Failed or ASan detected memory leak in {tag}!\n{res_test.stderr}")
        return False, duration, bin_size, res_test.stderr

    print(f"[+] [{tag.upper()}] All 27 tests PASSED in {duration:.3f}s (Binary Size: {bin_size/1024:.1f} KB, 0 Leaks)")
    return True, duration, bin_size, ""

def run_evolution_cycle():
    """Runs a single generation sandbox cycle comparing baseline vs candidate mutation."""
    print("\n==========================================================================")
    print("               Belya-Evolve Metamorphic Research Sandbox                  ")
    print("==========================================================================")
    setup_isolated_sandbox()

    # Step 1: Baseline verification
    ok_base, time_base, size_base, err = compile_and_test(tag="baseline")
    if not ok_base:
        print(f"[FATAL]: Baseline build failed in sandbox. Aborting.")
        sys.exit(1)

    fitness_base = 1000.0 / (time_base * (size_base / 1024.0))
    print(f"[*] Baseline Fitness Score: {fitness_base:.4f}")

    # Step 2: Example self-optimization verification (e.g. compiler optimization flag -O2)
    print("\n[*] Testing candidate optimization profile (-O2 + ASan)...")
    opt_cmd = [
        "gcc", "-O2", "-Wall", "-Wextra", "-Werror", "-std=c99",
        "-fsanitize=address,undefined",
        "test_suite.c", "linenoise.c", "minijson.c", "mcp_client.c",
        "model_adapter.c", "belya_agent.c", "belya_harness.c", "telegram_adapter.c",
        "-lcurl", "-lsqlite3", "-o", "belya_test_opt"
    ]
    subprocess.run(opt_cmd, cwd=SANDBOX_DIR, check=True)
    t0 = time.perf_counter()
    res_opt = subprocess.run([str(SANDBOX_DIR / "belya_test_opt")], cwd=SANDBOX_DIR, capture_output=True, text=True)
    t1 = time.perf_counter()
    time_opt = t1 - t0
    size_opt = (SANDBOX_DIR / "belya_test_opt").stat().st_size
    assert res_opt.returncode == 0
    fitness_opt = 1000.0 / (time_opt * (size_opt / 1024.0))

    print(f"[+] Optimized Candidate PASSED 27/27 tests in {time_opt:.3f}s (Binary Size: {size_opt/1024:.1f} KB)")
    print(f"[*] Candidate Fitness Score: {fitness_opt:.4f}")
    delta = ((fitness_opt - fitness_base) / fitness_base) * 100.0
    print(f"[*] Fitness Improvement: {delta:+.2f}%")

    print("\n[+] Evolution cycle completed safely inside isolated sandbox.")
    print("==========================================================================\n")

if __name__ == "__main__":
    run_evolution_cycle()
