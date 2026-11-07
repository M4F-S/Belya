---
name: qa-test-automation
description: Rigorous test automation, edge-case probing, sanitizer verifications, and regression suite execution.
triggers: [qa, test, automation, regression, sanitizer, asan, ubsan, fuzzing, verification]
category: quality
---

# QA & Test Automation Protocol

1. **Multi-Tier Test Verification:**
   - Unit Testing: Validate individual functions and algorithms in isolation with mock boundaries.
   - Integration Testing: Exercise full round-trip workflows through real schemas and network sockets.
   - Regression Testing: Run the complete test battery (`make test`) to ensure zero behavioral regression.

2. **Adversarial Edge-Case Probing:**
   - Supply empty buffers, maximum-length strings, negative indices, and invalid UTF-8 sequences.
   - Simulate out-of-memory (OOM) conditions by injecting allocation failures.
   - Test race conditions and concurrent access on shared resources.

3. **Sanitizer Zero-Tolerance:**
   - Compile and execute all test targets with AddressSanitizer (`-fsanitize=address`) and UBSan (`-fsanitize=undefined`).
   - Treat any memory leak, heap-buffer-overflow, or undefined behavior as an immediate blocking failure.
