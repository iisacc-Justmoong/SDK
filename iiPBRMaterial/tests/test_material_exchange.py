"""Check that Python exposes the same bytes, metadata and diagnostics as C++."""

import hashlib
import json
import pathlib
import sys
import unittest
import xml.etree.ElementTree as ET

LIBRARY, MODULE_DIR, FIXTURES, ENABLED = sys.argv[1:5]
sys.path.insert(0, MODULE_DIR)
from material_exchange import LibraryDocument, inspect_materialx


class ExchangeContract(unittest.TestCase):
    def test_reference_texture_metadata_matches_bytes_and_graph(self):
        root = pathlib.Path(FIXTURES)
        metadata = json.loads((root / "paint-reference.json").read_text(encoding="utf-8"))
        image = (root / metadata["asset"]).read_bytes()
        self.assertEqual(hashlib.sha256(image).hexdigest(), metadata["sha256"])
        self.assertEqual(image[:11], b"P6\n2 2\n255\n")
        self.assertEqual(len(image[11:]), 2 * 2 * 3)
        self.assertEqual(metadata["channels"], ["red", "green", "blue"])
        self.assertEqual(metadata["alpha"], "none")
        document = ET.fromstring((root / "standard-image.mtlx").read_bytes())
        file_input = document.find("./image/input[@name='file']")
        self.assertEqual(file_input.get("value"), metadata["asset"])
        self.assertEqual(file_input.get("colorspace"), metadata["color_space"])
        self.assertEqual(document.get("colorspace"), metadata["materialx_working_color_space"])

    def test_custom_library_and_preservation(self):
        root = pathlib.Path(FIXTURES)
        original = (root / "custom-node.mtlx").read_bytes()
        xml = (root / "custom-library.mtlx").read_bytes()
        digest = hashlib.sha256(xml).hexdigest()
        library = LibraryDocument("사용자", "1.0", digest, xml)
        result = inspect_materialx(LIBRARY, original, [library])
        self.assertEqual(result.original, original)
        self.assertEqual(result.libraries[0].id, "사용자")
        self.assertEqual(result.libraries[0].version, "1.0")
        self.assertEqual(result.libraries[0].sha256, digest)
        self.assertIsNone(result.converted)
        codes = {entry.code for entry in result.diagnostics}
        if ENABLED == "1":
            self.assertTrue(result.backend_available and result.parsed and result.native_valid)
            self.assertIn("library_hash_unverified", codes)
            self.assertIn("validation_scope_limited", codes)
            custom = next(item for item in result.nodes if item.path == "custom")
            self.assertEqual((custom.requested_version, custom.resolved_version), ("1.0", "1.0"))
            self.assertEqual((custom.library_id, custom.library_sha256), ("사용자", digest))
        else:
            self.assertFalse(result.backend_available or result.parsed)
            self.assertIn("backend_unavailable", codes)

    def test_unknown_is_never_silently_converted(self):
        original = (pathlib.Path(FIXTURES) / "unknown-node.mtlx").read_bytes()
        result = inspect_materialx(LIBRARY, original, [], convert=True)
        self.assertEqual(result.original, original)
        self.assertIsNone(result.converted)
        self.assertIn("conversion_unsupported", {entry.code for entry in result.diagnostics})
        if ENABLED == "1":
            self.assertIn("unresolved_node", {entry.code for entry in result.diagnostics})


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
