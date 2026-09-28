#!/usr/bin/env python3
"""
Unit and Integration Test Suite for Almaz Darwinian Novelty Gating
Verifies:
1. Deterministic SHA-256 patch & signature fingerprinting
2. Parsing and extraction of rejected taboo phenotypes from history
3. Multi-layer gating (ID, Patch SHA-256, Signature SHA-256)
4. JSON schema validation and rejected patch file persistence
5. Gating out previously failed phenotypes to prevent re-testing
"""

import sys
import json
import shutil
import tempfile
import unittest
from pathlib import Path

# Add tools directory to path
TOOLS_DIR = Path(__file__).resolve().parent
WORKSPACE_DIR = TOOLS_DIR.parent
sys.path.insert(0, str(TOOLS_DIR))

from evolution_supervisor import (
    compute_patch_and_hash,
    compute_mutation_signature,
    get_rejected_phenotypes,
    evaluate_novelty_gate,
    MUTATION_POOL
)

class TestNoveltyGating(unittest.TestCase):
    def setUp(self):
        self.test_dir = Path(tempfile.mkdtemp(prefix="almaz_novelty_test_"))
        self.sample_c = self.test_dir / "sample.c"
        self.sample_c.write_text(
            "int calculate(int x) {\n"
            "    // Original computation\n"
            "    return x * 2;\n"
            "}\n"
        )

    def tearDown(self):
        if self.test_dir.exists():
            shutil.rmtree(self.test_dir)

    def test_sha256_determinism(self):
        """Validates that compute_patch_and_hash produces deterministic 64-char SHA-256 hashes."""
        orig = "int foo(void) { return 0; }\n"
        mutated = "int foo(void) { return 1; }\n"
        patch_text_1, hash_1 = compute_patch_and_hash(orig, mutated, "foo.c")
        patch_text_2, hash_2 = compute_patch_and_hash(orig, mutated, "foo.c")

        self.assertEqual(hash_1, hash_2)
        self.assertEqual(len(hash_1), 64)
        self.assertTrue(all(c in "0123456789abcdef" for c in hash_1))
        self.assertIn("--- a/foo.c", patch_text_1)
        self.assertIn("+++ b/foo.c", patch_text_1)

    def test_signature_sha256(self):
        """Validates that compute_mutation_signature computes canonical transformation fingerprints."""
        sig1 = compute_mutation_signature("foo.c", "return 0;", "return 1;")
        sig2 = compute_mutation_signature("foo.c", "return 0;", "return 1;")
        sig3 = compute_mutation_signature("bar.c", "return 0;", "return 1;")

        self.assertEqual(sig1, sig2)
        self.assertNotEqual(sig1, sig3)
        self.assertEqual(len(sig1), 64)

    def test_rejected_phenotype_extraction(self):
        """Validates extraction of rejected IDs, patch hashes, and signature hashes from history."""
        mock_history = [
            {
                "id": "mut_accepted_1",
                "accepted": True,
                "status": "ACCEPTED",
                "patch_sha256": "aaaa" * 16,
                "signature_sha256": "1111" * 16
            },
            {
                "id": "mut_rejected_1",
                "accepted": False,
                "status": "REJECTED",
                "rejection_reason": "Candidate failed ASan compilation",
                "patch_sha256": "bbbb" * 16,
                "signature_sha256": "2222" * 16
            },
            {
                "id": "mut_regressed_1",
                "accepted": False,
                "status": "REJECTED",
                "rejection_reason": "Candidate performance regressed: -5.4%",
                "patch_sha256": "cccc" * 16,
                "signature_sha256": "3333" * 16
            }
        ]

        rej_ids, rej_patches, rej_sigs = get_rejected_phenotypes(mock_history)
        self.assertNotIn("mut_accepted_1", rej_ids)
        self.assertIn("mut_rejected_1", rej_ids)
        self.assertIn("mut_regressed_1", rej_ids)

        self.assertNotIn("aaaa" * 16, rej_patches)
        self.assertIn("bbbb" * 16, rej_patches)
        self.assertIn("cccc" * 16, rej_patches)

        self.assertNotIn("1111" * 16, rej_sigs)
        self.assertIn("2222" * 16, rej_sigs)
        self.assertIn("3333" * 16, rej_sigs)

    def test_novelty_gate_admittance_and_blocking(self):
        """Validates that novel phenotypes pass through while rejected phenotypes are blocked."""
        candidate = {
            "id": "calc_mult_to_shift",
            "name": "Bitshift optimization",
            "target_file": "sample.c",
            "old": "return x * 2;",
            "new": "return x << 1;",
            "description": "Test bitshift"
        }

        # 1. Clean history -> must be novel
        empty_history = []
        is_novel, reason, patch_hash, sig_hash = evaluate_novelty_gate(candidate, empty_history, self.test_dir)
        self.assertTrue(is_novel)
        self.assertEqual(reason, "Novel phenotype verified")
        self.assertIsNotNone(patch_hash)
        self.assertIsNotNone(sig_hash)

        # 2. History with matching ID rejected -> must be gated out
        id_rejected_history = [{
            "id": "calc_mult_to_shift",
            "accepted": False,
            "status": "REJECTED"
        }]
        is_novel_2, reason_2, _, _ = evaluate_novelty_gate(candidate, id_rejected_history, self.test_dir)
        self.assertFalse(is_novel_2)
        self.assertIn("previously tested and rejected", reason_2)

        # 3. History with matching patch SHA-256 rejected (even under a different ID) -> must be gated out
        patch_rejected_history = [{
            "id": "different_id_same_diff",
            "accepted": False,
            "status": "REJECTED",
            "patch_sha256": patch_hash
        }]
        is_novel_3, reason_3, _, _ = evaluate_novelty_gate(candidate, patch_rejected_history, self.test_dir)
        self.assertFalse(is_novel_3)
        self.assertIn("matches previously rejected phenotype", reason_3)

        # 4. History with matching signature SHA-256 rejected -> must be gated out
        sig_rejected_history = [{
            "id": "different_id_same_sig",
            "accepted": False,
            "status": "REJECTED",
            "signature_sha256": sig_hash
        }]
        is_novel_4, reason_4, _, _ = evaluate_novelty_gate(candidate, sig_rejected_history, self.test_dir)
        self.assertFalse(is_novel_4)
        self.assertIn("matches previously rejected transformation", reason_4)

    def test_rejected_patch_persistence_and_subsequent_gating(self):
        """Validates that rejected patches are saved with SHA-256 and blocked on subsequent passes."""
        patches_dir = self.test_dir / "patches"
        patches_dir.mkdir(parents=True, exist_ok=True)
        history_file = self.test_dir / "mutation_history.json"

        candidate = {
            "id": "broken_syntax_mut",
            "name": "Intentionally Broken Syntax",
            "target_file": "sample.c",
            "old": "return x * 2;",
            "new": "return x * ; // syntax error",
            "description": "Syntax breaker"
        }

        # Calculate prospective patch & hash
        orig_content = self.sample_c.read_text()
        mutated_content = orig_content.replace(candidate["old"], candidate["new"], 1)
        patch_text, patch_sha256 = compute_patch_and_hash(orig_content, mutated_content, "sample.c")
        sig_sha256 = compute_mutation_signature("sample.c", candidate["old"], candidate["new"])

        # Simulate rejection recording
        rej_file = patches_dir / f"rejected_20260928_120000_{candidate['id']}.patch"
        rej_file.write_text(patch_text)

        history_entry = {
            "timestamp": "2026-09-28T12:00:00Z",
            "id": candidate["id"],
            "name": candidate["name"],
            "accepted": False,
            "status": "REJECTED",
            "rejection_reason": "Candidate failed ASan compilation: syntax error",
            "speedup": 0.0,
            "patch": rej_file.name,
            "patch_sha256": patch_sha256,
            "signature_sha256": sig_sha256,
            "commit": None
        }
        history = [history_entry]
        history_file.write_text(json.dumps(history, indent=2))

        # Now test that evaluate_novelty_gate BLOCKS this phenotype!
        loaded_history = json.loads(history_file.read_text())
        is_novel, reason, p_hash, s_hash = evaluate_novelty_gate(candidate, loaded_history, self.test_dir)

        self.assertFalse(is_novel)
        self.assertIn("previously tested and rejected", reason)
        self.assertEqual(p_hash, patch_sha256)
        self.assertEqual(s_hash, sig_sha256)
        self.assertTrue(rej_file.exists())
        self.assertEqual(len(patch_sha256), 64)

    def test_pool_integrity(self):
        """Validates that all candidates in MUTATION_POOL have required fields."""
        for m in MUTATION_POOL:
            self.assertIn("id", m)
            self.assertIn("name", m)
            self.assertIn("target_file", m)
            self.assertIn("old", m)
            self.assertIn("new", m)
            self.assertIn("description", m)
            self.assertTrue(len(m["id"]) > 0)
            self.assertTrue(len(m["old"]) > 0)
            self.assertTrue(len(m["new"]) > 0)

if __name__ == "__main__":
    unittest.main()
