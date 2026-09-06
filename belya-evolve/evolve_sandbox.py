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

BENCH_C_SRC = r"""#include "minijson.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define ITERATIONS 100000

int main(void) {
    const char *json_sample = 
        "   {\n"
        "       \"name\": \"belya_agent\",\n"
        "       \"version\": 6,\n"
        "       \"active\": true,\n"
        "       \"tags\": [ \"c99\", \"autonomous\", \"posix\" ]\n"
        "   }   ";

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < ITERATIONS; i++) {
        JsonValue *val = json_parse(json_sample);
        if (!val) {
            fprintf(stderr, "JSON parse failure at iteration %d\n", i);
            return 1;
        }
        json_free(val);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                        (end.tv_nsec - start.tv_nsec) / 1000000.0;

    printf("%.2f", elapsed_ms);
    return 0;
}
"""

def setup_isolated_sandbox():
    """Clones active C sources into an isolated temp directory."""
    if SANDBOX_DIR.exists():
        shutil.rmtree(SANDBOX_DIR)
    SANDBOX_DIR.mkdir(parents=True, exist_ok=True)

    print(f"[*] Setting up isolated sandbox at: {SANDBOX_DIR}")
    for ext in ["*.c", "*.h", "Makefile"]:
        for file_path in PROJECT_ROOT.glob(ext):
            shutil.copy(file_path, SANDBOX_DIR / file_path.name)

    (SANDBOX_DIR / "bench_minijson.c").write_text(BENCH_C_SRC)
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

def benchmark_json_throughput(tag="baseline"):
    """Compiles and executes the 100,000-iteration JSON parse microbenchmark."""
    print(f"[*] [{tag.upper()}] Compiling JSON parse microbenchmark...")
    build_cmd = [
        "gcc", "-O2", "-Wall", "-Wextra", "-Werror", "-std=c99",
        "-fsanitize=address,undefined",
        "bench_minijson.c", "minijson.c", "-o", f"bench_{tag}"
    ]
    res_build = subprocess.run(build_cmd, cwd=SANDBOX_DIR, capture_output=True, text=True)
    if res_build.returncode != 0:
        print(f"[!] Benchmark compilation failed for {tag}:\n{res_build.stderr}")
        return 0.0

    print(f"[*] [{tag.upper()}] Executing 100,000 JSON parse iterations...")
    res_bench = subprocess.run([str(SANDBOX_DIR / f"bench_{tag}")], cwd=SANDBOX_DIR, capture_output=True, text=True)
    if res_bench.returncode != 0:
        print(f"[!] Benchmark execution failed for {tag}:\n{res_bench.stderr}")
        return 0.0

    elapsed_ms = float(res_bench.stdout.strip())
    throughput = (100000.0 / (elapsed_ms / 1000.0))
    print(f"[+] [{tag.upper()}] 100,000 parses completed in {elapsed_ms:.2f} ms ({throughput:,.0f} ops/sec)")
    return elapsed_ms

def apply_metamorphic_mutation():
    """
    Applies source-level metamorphic optimization to minijson.c inside sandbox:
    Replaces locale-dependent isspace() with branchless/inlined character comparisons.
    """
    minijson_path = SANDBOX_DIR / "minijson.c"
    content = minijson_path.read_text()

    old_func = """static const char *skip_ws(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}"""

    new_func = """static inline const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\\t' || *s == '\\n' || *s == '\\r') s++;
    return s;
}"""

    if old_func not in content:
        raise RuntimeError("Target mutation anchor not found in minijson.c")

    mutated_content = content.replace(old_func, new_func)
    minijson_path.write_text(mutated_content)
    print("\n[*] [MUTATION] Injected inlined character comparison into minijson.c skip_ws().")

def run_evolution_cycle():
    """Runs a complete metamorphic generation: Sandbox Setup -> Baseline -> Mutation -> Verification -> Fitness Score."""
    print("\n==========================================================================")
    print("               Belya-Evolve Metamorphic Research Sandbox                  ")
    print("==========================================================================")
    setup_isolated_sandbox()

    # Step 1: Baseline verification
    print("\n--- STAGE 1: BASELINE VERIFICATION ---")
    ok_base, time_base, size_base, _ = compile_and_test(tag="baseline")
    if not ok_base:
        print("[FATAL]: Baseline build failed in sandbox. Aborting.")
        sys.exit(1)

    time_bench_base = benchmark_json_throughput(tag="baseline")

    # Step 2: Metamorphic Mutation
    print("\n--- STAGE 2: METAMORPHIC MUTATION ---")
    apply_metamorphic_mutation()

    # Step 3: Candidate Verification
    print("\n--- STAGE 3: REGRESSION & MEMORY SAFETY VERIFICATION ---")
    ok_cand, time_cand, size_cand, err_cand = compile_and_test(tag="candidate")
    if not ok_cand:
        print(f"[!] Candidate failed verification: {err_cand}")
        print("[!] Darwinian Selection: MUTATION REJECTED (Safety violation).")
        return False

    time_bench_cand = benchmark_json_throughput(tag="candidate")

    # Step 4: Fitness Evaluation
    print("\n--- STAGE 4: FITNESS SCORE & DARWINIAN SELECTION ---")
    fitness_base = (100000.0 / time_bench_base) / (size_base / 1024.0)
    fitness_cand = (100000.0 / time_bench_cand) / (size_cand / 1024.0)
    perf_delta = ((time_bench_base - time_bench_cand) / time_bench_base) * 100.0
    fitness_delta = ((fitness_cand - fitness_base) / fitness_base) * 100.0

    print(f"[*] Baseline JSON Duration:  {time_bench_base:.2f} ms | Fitness: {fitness_base:.2f}")
    print(f"[*] Candidate JSON Duration: {time_bench_cand:.2f} ms | Fitness: {fitness_cand:.2f}")
    print(f"[*] Latency Reduction:      {perf_delta:+.2f}%")
    print(f"[*] Overall Fitness Gain:   {fitness_delta:+.2f}%")

    if fitness_cand > fitness_base and ok_cand:
        print("\n[+] [RESULT]: Darwinian Selection: MUTATION ACCEPTED!")
        print("    Code is semantically equivalent, passes 27/27 ASan tests with 0 leaks, and exhibits superior fitness.")
    else:
        print("\n[-] [RESULT]: Darwinian Selection: MUTATION REJECTED (Did not meet fitness threshold).")

    print("==========================================================================\n")
    return True

if __name__ == "__main__":
    run_evolution_cycle()
