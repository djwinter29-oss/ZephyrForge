"""Regression tests for local Markdown link and heading validation."""

import importlib.util
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER_SPEC = importlib.util.spec_from_file_location("check_docs", ROOT / "tools/check_docs.py")
CHECKER = importlib.util.module_from_spec(CHECKER_SPEC)
CHECKER_SPEC.loader.exec_module(CHECKER)


class DocumentationLinkTests(unittest.TestCase):
    def test_heading_anchors_support_markdown_and_duplicate_headings(self):
        anchors = CHECKER.heading_anchors(
            "# A `Code` heading\n# A Code heading\n# [Linked](https://example.test) heading\n"
            "# Fish &amp; chips\n```\n# Not a heading\n```\n"
        )
        self.assertEqual(
            anchors, {"a-code-heading", "a-code-heading-1", "linked-heading", "fish--chips"}
        )

    def test_local_and_cross_file_fragments_are_checked(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "index.md"
            target = root / "guide.md"
            source.write_text("# Start\n", encoding="utf-8")
            target.write_text("# Setup\n", encoding="utf-8")

            self.assertIsNone(CHECKER.link_failure(source, "#start"))
            self.assertIsNone(CHECKER.link_failure(source, "guide.md#setup"))
            self.assertIn("missing fragment", CHECKER.link_failure(source, "#missing"))
            self.assertIn("missing fragment", CHECKER.link_failure(source, "guide.md#missing"))
            self.assertIn("missing ", CHECKER.link_failure(source, "absent.md#setup"))


if __name__ == "__main__":
    unittest.main()
