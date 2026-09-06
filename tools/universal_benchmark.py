#!/usr/bin/env python3
"""
Belya Universal Benchmark Suite (Exercism C Coding Benchmark)
Evaluates autonomous C99 code authoring, compiler diagnostics, and memory safety
against standard Exercism challenges used by Aider, Claude Code, and SWE-agent.
"""

import os
import sys
import time
import shutil
import subprocess
from pathlib import Path

BASE_DIR = Path("/tmp/belya_benchmarks")
PROJECT_ROOT = Path("/Users/mohamedfathy/Desktop/Privacy C/C Harness and Agent")
BELYA_BIN = PROJECT_ROOT / "belya"
ENV_FILE = PROJECT_ROOT / ".env"
AGENT_RULES = PROJECT_ROOT / ".agentrules"

# If .env not in project root, copy from /opt/belya on VPS
if not ENV_FILE.exists():
    vps_env = Path("/tmp/belya_vps.env")
    if not vps_env.exists():
        subprocess.run(["scp", "root@187.124.2.26:/opt/belya/.env", str(vps_env)], check=True)
    shutil.copy(vps_env, ENV_FILE)

BENCHMARKS = [
    {
        "name": "binary_search",
        "title": "Binary Search",
        "description": "Implement binary search over a sorted integer array returning pointer to element or NULL.",
        "header_name": "binary_search.h",
        "header": """#ifndef BINARY_SEARCH_H
#define BINARY_SEARCH_H

#include <stddef.h>

const int *binary_search(int value, const int *arr, size_t length);

#endif
""",
        "test_name": "test_binary_search.c",
        "test_code": """#include "binary_search.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    printf("[Test] Binary Search Suite...\\n");
    int arr[] = {1, 3, 4, 6, 8, 9, 11};
    size_t len = sizeof(arr) / sizeof(arr[0]);

    // 1. Find element in middle
    const int *res1 = binary_search(6, arr, len);
    assert(res1 != NULL && *res1 == 6);

    // 2. Find element at beginning
    const int *res2 = binary_search(1, arr, len);
    assert(res2 != NULL && *res2 == 1);

    // 3. Find element at end
    const int *res3 = binary_search(11, arr, len);
    assert(res3 != NULL && *res3 == 11);

    // 4. Element not present
    const int *res4 = binary_search(7, arr, len);
    assert(res4 == NULL);

    // 5. Single element array
    int single[] = {42};
    assert(binary_search(42, single, 1) == &single[0]);
    assert(binary_search(0, single, 1) == NULL);

    // 6. Empty array
    assert(binary_search(5, NULL, 0) == NULL);

    printf("  -> Binary Search PASSED!\\n");
    return 0;
}
""",
        "makefile": """CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -fsanitize=address,undefined

test: binary_search.o test_binary_search.o
\t$(CC) $(CFLAGS) -o test_runner test_binary_search.o binary_search.o
\t./test_runner

binary_search.o: binary_search.c binary_search.h
\t$(CC) $(CFLAGS) -c binary_search.c -o binary_search.o

test_binary_search.o: test_binary_search.c binary_search.h
\t$(CC) $(CFLAGS) -c test_binary_search.c -o test_binary_search.o

clean:
\trm -f *.o test_runner
"""
    },
    {
        "name": "queen_attack",
        "title": "Queen Attack",
        "description": "Calculate chess queen attack geometry (same row, same column, or diagonal) with coordinate validation.",
        "header_name": "queen_attack.h",
        "header": """#ifndef QUEEN_ATTACK_H
#define QUEEN_ATTACK_H

#include <stdint.h>

typedef struct {
    uint8_t row;
    uint8_t column;
} position_t;

typedef enum {
    ATTACK_FALSE,
    ATTACK_TRUE,
    ATTACK_INVALID
} attack_result_t;

attack_result_t can_attack(position_t white_queen, position_t black_queen);

#endif
""",
        "test_name": "test_queen_attack.c",
        "test_code": """#include "queen_attack.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    printf("[Test] Queen Attack Suite...\\n");

    // 1. Invalid positions (>= 8)
    assert(can_attack((position_t){8, 4}, (position_t){2, 4}) == ATTACK_INVALID);
    assert(can_attack((position_t){2, 8}, (position_t){2, 4}) == ATTACK_INVALID);

    // 2. Same position
    assert(can_attack((position_t){2, 2}, (position_t){2, 2}) == ATTACK_INVALID);

    // 3. Cannot attack
    assert(can_attack((position_t){2, 4}, (position_t){6, 6}) == ATTACK_FALSE);

    // 4. Can attack same row
    assert(can_attack((position_t){2, 4}, (position_t){2, 6}) == ATTACK_TRUE);

    // 5. Can attack same column
    assert(can_attack((position_t){4, 5}, (position_t){2, 5}) == ATTACK_TRUE);

    // 6. Can attack on diagonal (positive & negative slopes)
    assert(can_attack((position_t){2, 2}, (position_t){0, 4}) == ATTACK_TRUE);
    assert(can_attack((position_t){2, 2}, (position_t){3, 1}) == ATTACK_TRUE);
    assert(can_attack((position_t){1, 1}, (position_t){6, 6}) == ATTACK_TRUE);

    printf("  -> Queen Attack PASSED!\\n");
    return 0;
}
""",
        "makefile": """CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -fsanitize=address,undefined

test: queen_attack.o test_queen_attack.o
\t$(CC) $(CFLAGS) -o test_runner test_queen_attack.o queen_attack.o
\t./test_runner

queen_attack.o: queen_attack.c queen_attack.h
\t$(CC) $(CFLAGS) -c queen_attack.c -o queen_attack.o

test_queen_attack.o: test_queen_attack.c queen_attack.h
\t$(CC) $(CFLAGS) -c test_queen_attack.c -o test_queen_attack.o

clean:
\trm -f *.o test_runner
"""
    },
    {
        "name": "roman_numerals",
        "title": "Roman Numerals",
        "description": "Convert unsigned integers to dynamically allocated Roman Numeral strings using standard subtractive notation.",
        "header_name": "roman_numerals.h",
        "header": """#ifndef ROMAN_NUMERALS_H
#define ROMAN_NUMERALS_H

char *to_roman_numeral(unsigned int number);

#endif
""",
        "test_name": "test_roman_numerals.c",
        "test_code": """#include "roman_numerals.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_roman(unsigned int num, const char *expected) {
    char *actual = to_roman_numeral(num);
    assert(actual != NULL);
    assert(strcmp(actual, expected) == 0);
    free(actual);
}

int main(void) {
    printf("[Test] Roman Numerals Suite...\\n");
    check_roman(1, "I");
    check_roman(2, "II");
    check_roman(4, "IV");
    check_roman(5, "V");
    check_roman(9, "IX");
    check_roman(27, "XXVII");
    check_roman(48, "XLVIII");
    check_roman(59, "LIX");
    check_roman(93, "XCIII");
    check_roman(141, "CXLI");
    check_roman(163, "CLXIII");
    check_roman(402, "CDII");
    check_roman(575, "DLXXV");
    check_roman(911, "CMXI");
    check_roman(1024, "MXXIV");
    check_roman(3000, "MMM");
    printf("  -> Roman Numerals PASSED!\\n");
    return 0;
}
""",
        "makefile": """CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -fsanitize=address,undefined

test: roman_numerals.o test_roman_numerals.o
\t$(CC) $(CFLAGS) -o test_runner test_roman_numerals.o roman_numerals.o
\t./test_runner

roman_numerals.o: roman_numerals.c roman_numerals.h
\t$(CC) $(CFLAGS) -c roman_numerals.c -o roman_numerals.o

test_roman_numerals.o: test_roman_numerals.c roman_numerals.h
\t$(CC) $(CFLAGS) -c test_roman_numerals.c -o test_roman_numerals.o

clean:
\trm -f *.o test_runner
"""
    },
    {
        "name": "circular_buffer",
        "title": "Circular Buffer",
        "description": "Implement a FIFO circular buffer with read, write, overwrite, and dynamic allocation.",
        "header_name": "circular_buffer.h",
        "header": """#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <stddef.h>
#include <stdint.h>

typedef int buffer_value_t;
typedef struct circular_buffer_s circular_buffer_t;

typedef enum {
    BUFFER_OK,
    BUFFER_FULL,
    BUFFER_EMPTY
} buffer_status_t;

circular_buffer_t *new_circular_buffer(size_t capacity);
buffer_status_t write(circular_buffer_t *buffer, buffer_value_t value);
buffer_status_t overwrite(circular_buffer_t *buffer, buffer_value_t value);
buffer_status_t read(circular_buffer_t *buffer, buffer_value_t *value);
void clear_buffer(circular_buffer_t *buffer);
void delete_buffer(circular_buffer_t *buffer);

#endif
""",
        "test_name": "test_circular_buffer.c",
        "test_code": """#include "circular_buffer.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    printf("[Test] Circular Buffer Suite...\\n");
    circular_buffer_t *buf = new_circular_buffer(2);
    assert(buf != NULL);

    buffer_value_t val = 0;
    // Empty read
    assert(read(buf, &val) == BUFFER_EMPTY);

    // Write 1, 2
    assert(write(buf, 1) == BUFFER_OK);
    assert(write(buf, 2) == BUFFER_OK);
    // Buffer full
    assert(write(buf, 3) == BUFFER_FULL);

    // Read 1
    assert(read(buf, &val) == BUFFER_OK && val == 1);

    // Write 3 now that space opened
    assert(write(buf, 3) == BUFFER_OK);

    // Overwrite when full
    assert(overwrite(buf, 4) == BUFFER_OK);

    // Values should now be 3, 4
    assert(read(buf, &val) == BUFFER_OK && val == 3);
    assert(read(buf, &val) == BUFFER_OK && val == 4);
    assert(read(buf, &val) == BUFFER_EMPTY);

    // Clear buffer
    assert(write(buf, 10) == BUFFER_OK);
    clear_buffer(buf);
    assert(read(buf, &val) == BUFFER_EMPTY);

    delete_buffer(buf);
    printf("  -> Circular Buffer PASSED!\\n");
    return 0;
}
""",
        "makefile": """CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -fsanitize=address,undefined

test: circular_buffer.o test_circular_buffer.o
\t$(CC) $(CFLAGS) -o test_runner test_circular_buffer.o circular_buffer.o
\t./test_runner

circular_buffer.o: circular_buffer.c circular_buffer.h
\t$(CC) $(CFLAGS) -c circular_buffer.c -o circular_buffer.o

test_circular_buffer.o: test_circular_buffer.c circular_buffer.h
\t$(CC) $(CFLAGS) -c test_circular_buffer.c -o test_circular_buffer.o

clean:
\trm -f *.o test_runner
"""
    },
    {
        "name": "word_count",
        "title": "Word Count",
        "description": "Parse a sentence, tokenize words ignoring punctuation, normalize lowercase, and count word frequencies.",
        "header_name": "word_count.h",
        "header": """#ifndef WORD_COUNT_H
#define WORD_COUNT_H

#define MAX_WORDS 20
#define MAX_WORD_LENGTH 50

typedef struct word_count_word {
    char text[MAX_WORD_LENGTH + 1];
    int count;
} word_count_word_t;

int count_words(const char *sentence, word_count_word_t *words);

#endif
""",
        "test_name": "test_word_count.c",
        "test_code": """#include "word_count.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int find_count(const char *word, word_count_word_t *words, int count) {
    for (int i = 0; i < count; i++) {
        if (strcmp(words[i].text, word) == 0) return words[i].count;
    }
    return 0;
}

int main(void) {
    printf("[Test] Word Count Suite...\\n");
    word_count_word_t words[MAX_WORDS];
    memset(words, 0, sizeof(words));

    // Test 1: Single word
    int c1 = count_words("word", words);
    assert(c1 == 1);
    assert(find_count("word", words, c1) == 1);

    // Test 2: Multiple occurrences & case-insensitivity
    memset(words, 0, sizeof(words));
    int c2 = count_words("go Go GO Stop stop", words);
    assert(c2 == 2);
    assert(find_count("go", words, c2) == 3);
    assert(find_count("stop", words, c2) == 2);

    // Test 3: Punctuation handled cleanly
    memset(words, 0, sizeof(words));
    int c3 = count_words("car : carpet as java : javascript!!&@$%^&", words);
    assert(c3 == 5);
    assert(find_count("car", words, c3) == 1);
    assert(find_count("carpet", words, c3) == 1);
    assert(find_count("as", words, c3) == 1);
    assert(find_count("java", words, c3) == 1);
    assert(find_count("javascript", words, c3) == 1);

    printf("  -> Word Count PASSED!\\n");
    return 0;
}
""",
        "makefile": """CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -fsanitize=address,undefined

test: word_count.o test_word_count.o
\t$(CC) $(CFLAGS) -o test_runner test_word_count.o word_count.o
\t./test_runner

word_count.o: word_count.c word_count.h
\t$(CC) $(CFLAGS) -c word_count.c -o word_count.o

test_word_count.o: test_word_count.c word_count.h
\t$(CC) $(CFLAGS) -c test_word_count.c -o test_word_count.o

clean:
\trm -f *.o test_runner
"""
    }
]

def run_benchmark():
    if BASE_DIR.exists():
        shutil.rmtree(BASE_DIR)
    BASE_DIR.mkdir(parents=True, exist_ok=True)

    print("==========================================================================")
    print("      BELYA AUTONOMOUS UNIVERSAL BENCHMARK (AIDER / EXERCISM C SUITE)      ")
    print("==========================================================================")
    print(f"Belya Binary: {BELYA_BIN}")
    print(f"Total Standardized Challenges: {len(BENCHMARKS)}")
    print("Compiler Flags: -Wall -Wextra -std=c99 -fsanitize=address,undefined")
    print("==========================================================================\n")

    results = []

    for idx, bench in enumerate(BENCHMARKS, 1):
        b_name = bench["name"]
        b_dir = BASE_DIR / b_name
        b_dir.mkdir(parents=True, exist_ok=True)

        # Write exercise files
        (b_dir / bench["header_name"]).write_text(bench["header"])
        (b_dir / bench["test_name"]).write_text(bench["test_code"])
        (b_dir / "Makefile").write_text(bench["makefile"])

        # Setup git repo in exercise directory
        subprocess.run(["git", "init"], cwd=b_dir, capture_output=True)
        subprocess.run(["git", "config", "user.name", "BenchmarkRunner"], cwd=b_dir, capture_output=True)
        subprocess.run(["git", "config", "user.email", "bench@belya.local"], cwd=b_dir, capture_output=True)
        subprocess.run(["git", "add", "."], cwd=b_dir, capture_output=True)
        subprocess.run(["git", "commit", "-m", "Initial commit"], cwd=b_dir, capture_output=True)

        # Copy config
        shutil.copy(ENV_FILE, b_dir / ".env")
        shutil.copy(AGENT_RULES, b_dir / ".agentrules")

        prompt = (
            f"You are working on the '{b_name}' exercise. "
            f"Inspect '{bench['header_name']}' and '{bench['test_name']}' using read_file to understand requirements. "
            f"Implement the solution in '{b_name}.c' using write_file. "
            f"Compile and run tests with 'make test' using bash. "
            f"Ensure all tests pass cleanly under AddressSanitizer with zero compiler warnings and zero memory leaks."
        )

        print(f"[{idx}/{len(BENCHMARKS)}] Running Benchmark: {bench['title']} ({b_name})...")
        t0 = time.time()

        proc = subprocess.run(
            [str(BELYA_BIN), "--prompt", prompt],
            cwd=b_dir,
            capture_output=True,
            text=True,
            timeout=180
        )
        duration = time.time() - t0

        # Verify whether make test passes now
        test_run = subprocess.run(
            ["make", "test"],
            cwd=b_dir,
            capture_output=True,
            text=True
        )

        passed = (test_run.returncode == 0) and ("PASSED" in test_run.stdout)

        # Count tool turns from belya output
        tool_turns = proc.stdout.count("[Tool Call Request]:")

        # Check line count of generated C file
        c_file = b_dir / f"{b_name}.c"
        loc = len(c_file.read_text().splitlines()) if c_file.exists() else 0

        status_str = "\033[1;32mPASSED\033[0m" if passed else "\033[1;31mFAILED\033[0m"
        print(f"   -> Result: {status_str} | Duration: {duration:.2f}s | Steps: {tool_turns} | Code: {loc} LOC")

        results.append({
            "name": bench["title"],
            "id": b_name,
            "passed": passed,
            "duration": duration,
            "steps": tool_turns,
            "loc": loc
        })

    # Summary Table
    total_passed = sum(1 for r in results if r["passed"])
    pass_rate = (total_passed / len(results)) * 100.0
    total_time = sum(r["duration"] for r in results)

    print("\n==========================================================================")
    print("                     UNIVERSAL BENCHMARK FINAL SCORECARD                  ")
    print("==========================================================================")
    print(f"{'Challenge':<20} | {'Status':<8} | {'Duration':<10} | {'Steps':<6} | {'LOC':<6}")
    print("-" * 60)
    for r in results:
        res = "PASSED" if r["passed"] else "FAILED"
        print(f"{r['name']:<20} | {res:<8} | {r['duration']:>7.2f}s   | {r['steps']:<6} | {r['loc']:<6}")
    print("=" * 60)
    print(f"Overall Pass Rate (Pass@1): {total_passed}/{len(results)} ({pass_rate:.1f}%)")
    print(f"Total Benchmark Duration:   {total_time:.2f}s")
    print("==========================================================================\n")

    return results

if __name__ == "__main__":
    run_benchmark()
