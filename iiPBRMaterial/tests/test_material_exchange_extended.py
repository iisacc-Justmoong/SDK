"""Independent Python checks of the native strict exchange and texture adapter."""

import hashlib
import json
import pathlib
import binascii
import struct
import sys
import unittest
import xml.etree.ElementTree as ET
import zlib

LIBRARY, MODULE_DIR, FIXTURES, ENABLED, STDLIB = sys.argv[1:6]
sys.path.insert(0, MODULE_DIR)
from material_exchange import (
    LibraryDocument, MaterialExchangeResult, MaterialXTextureConverter, TextureBinding, TextureMetadata, convert_texture_to_materialx,
    inspect_materialx_strict, sha256_bytes,
)


class ExtendedExchangeContract(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path(FIXTURES)
        self.custom = (self.root / "custom-library.mtlx").read_bytes()
        self.document = (self.root / "custom-node.mtlx").read_bytes()
        self.texture = (self.root / "paint-reference.ppm").read_bytes()

    def binding(self, **overrides):
        values = dict(original=self.texture, sha256=hashlib.sha256(self.texture).hexdigest(),
                      uri="textures/paint&color.ppm", color_space="srgb_texture",
                      working_color_space="lin_rec709", alpha="none",
                      channels=("red", "green", "blue"), semantic="base_color",
                      node_name="paint_color", u_address="clamp", v_address="periodic", filtering="linear")
        values.update(overrides)
        return TextureBinding(**values)

    def stdlib(self):
        if ENABLED != "1":
            return []
        data = (pathlib.Path(STDLIB) / "stdlib/stdlib_defs.mtlx").read_bytes()
        return [LibraryDocument("stdlib", "1.39.5", hashlib.sha256(data).hexdigest(), data)]

    def codes(self, result):
        return {item.code for item in result.diagnostics}

    def test_sha256_matches_python_on_binary_and_block_boundaries(self):
        for length in (0, 1, 55, 56, 63, 64, 65, 127, 128, 129, 1000, 1_048_576):
            data = bytes((index * 137 + 41) & 255 for index in range(length))
            self.assertEqual(sha256_bytes(LIBRARY, data), hashlib.sha256(data).hexdigest())

    def test_strict_library_hash_match_and_mismatch(self):
        digest = hashlib.sha256(self.custom).hexdigest()
        valid = [LibraryDocument("custom", "1.0", digest, self.custom)]
        result = inspect_materialx_strict(LIBRARY, self.document, valid)
        self.assertEqual(result.original, self.document)
        self.assertNotIn("library_hash_mismatch", self.codes(result))
        if ENABLED == "1":
            self.assertTrue(result.parsed and result.native_valid)
            self.assertNotIn("library_hash_unverified", self.codes(result))
        else:
            self.assertIn("backend_unavailable", self.codes(result))
        tampered = [LibraryDocument("custom", "1.0", "0" * 64, self.custom)]
        rejected = inspect_materialx_strict(LIBRARY, self.document, tampered)
        self.assertEqual(rejected.original, self.document)
        self.assertFalse(rejected.parsed)
        self.assertIn("library_hash_mismatch", self.codes(rejected))
        uppercase = [LibraryDocument("custom", "1.0", digest.upper(), self.custom)]
        self.assertNotIn("library_hash_mismatch", self.codes(inspect_materialx_strict(LIBRARY, self.document, uppercase)))

    def test_strict_duplicate_siblings_fail_without_losing_original(self):
        original = (self.root / "duplicate-name.mtlx").read_bytes()
        result = inspect_materialx_strict(LIBRARY, original, [])
        self.assertEqual(result.original, original)
        self.assertFalse(result.parsed)
        self.assertIn("duplicate_element_name", self.codes(result))

    def test_strict_rejects_ambiguous_entity_names_and_doctype(self):
        for original in (b'<materialx><x name="a&amp;b"/></materialx>',
                         b'<!DOCTYPE materialx><materialx/>'):
            result = inspect_materialx_strict(LIBRARY, original, [])
            self.assertEqual(result.original, original)
            self.assertFalse(result.parsed)
            self.assertIn("strict_xml_unsupported", self.codes(result))

    def test_strict_names_are_scoped_and_ignore_comments(self):
        original = (b'<materialx version="1.39"><!-- <x name="repeat"/> -->'
                    b'<nodegraph name="a"><x name="repeat" type="float"/></nodegraph>'
                    b'<nodegraph name="b"><x name="repeat" type="float"/></nodegraph></materialx>')
        result = inspect_materialx_strict(LIBRARY, original, [])
        self.assertEqual(result.original, original)
        self.assertNotIn("duplicate_element_name", self.codes(result))
        self.assertNotIn("strict_xml_unsupported", self.codes(result))

    def test_image_conversion_validates_explicit_metadata(self):
        result = convert_texture_to_materialx(LIBRARY, self.binding(), self.stdlib())
        self.assertEqual(result.original, self.texture)
        self.assertEqual(result.texture.channels, ("red", "green", "blue"))
        self.assertEqual(result.texture.alpha, "none")
        self.assertEqual(result.texture.sha256, hashlib.sha256(self.texture).hexdigest())
        if ENABLED != "1":
            self.assertIsNone(result.converted)
            self.assertIn("backend_unavailable", self.codes(result))
            return
        self.assertIsNotNone(result.converted)
        doc = ET.fromstring(result.converted)
        self.assertEqual(doc.get("colorspace"), "lin_rec709")
        image = doc.find("./image[@name='paint_color']")
        self.assertEqual(image.get("type"), "color3")
        file_input = image.find("./input[@name='file']")
        self.assertEqual(file_input.get("value"), "textures/paint&color.ppm")
        self.assertEqual(file_input.get("colorspace"), "srgb_texture")
        for field, expected in (("uaddressmode", "clamp"), ("vaddressmode", "periodic"), ("filtertype", "linear")):
            self.assertEqual(image.find(f"./input[@name='{field}']").get("value"), expected)
        self.assertIn("texture_content_unverified", self.codes(result))
        self.assertIn("uv_binding_required", self.codes(result))
        parsed = inspect_materialx_strict(LIBRARY, result.converted, self.stdlib())
        self.assertTrue(parsed.parsed and parsed.native_valid)
        self.assertNotIn("unresolved_node", self.codes(parsed))
        no_library = convert_texture_to_materialx(LIBRARY, self.binding(), [])
        self.assertIsNone(no_library.converted)
        self.assertIn("unresolved_node", self.codes(no_library))
        stdlib = self.stdlib()[0]
        tampered = LibraryDocument(stdlib.id, stdlib.version, "0" * 64, stdlib.xml)
        rejected = convert_texture_to_materialx(LIBRARY, self.binding(), [tampered])
        self.assertIsNone(rejected.converted)
        self.assertEqual(rejected.original, self.texture)
        self.assertIn("library_hash_mismatch", self.codes(rejected))

    def test_image_conversion_rejects_loss_and_hash_mismatch(self):
        for binding, code in ((self.binding(alpha="straight", channels=("red", "green", "blue", "alpha")),
                               "unsupported_texture_layout"),
                              (self.binding(color_space=""), "invalid_texture_metadata"),
                              (self.binding(uri="textures/a\nb.ppm"), "invalid_texture_metadata"),
                              (self.binding(node_name="unsafe name"), "invalid_texture_metadata"),
                              (self.binding(sha256="0" * 64), "texture_hash_mismatch")):
            result = convert_texture_to_materialx(LIBRARY, binding, self.stdlib())
            self.assertEqual(result.original, self.texture)
            self.assertEqual(result.texture.alpha, binding.alpha)
            self.assertEqual(result.texture.channels, binding.channels)
            self.assertIsNone(result.converted)
            self.assertIn(code, self.codes(result))

    def test_straight_alpha_rgba_png_preserves_metadata_and_channels(self):
        metadata = json.loads((self.root / "paint-reference-rgba.json").read_text())
        png = (self.root / metadata["asset"]).read_bytes()
        self.assertEqual(hashlib.sha256(png).hexdigest(), metadata["sha256"])
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        width, height, bits, color_type, _, _, _ = struct.unpack(">IIBBBBB", png[16:29])
        self.assertEqual((width, height, bits, color_type), (2, 2, 8, 6))
        idat_length = struct.unpack(">I", png[33:37])[0]
        self.assertEqual(png[37:41], b"IDAT")
        pixels = zlib.decompress(png[41:41 + idat_length])
        self.assertEqual((pixels[4], pixels[8], pixels[13], pixels[17]), (255, 128, 0, 64))
        binding = self.binding(original=png, sha256=metadata["sha256"], uri="paint-rgba.png",
                               alpha="straight", channels=("red", "green", "blue", "alpha"))
        premultiplied = self.binding(original=png, sha256=metadata["sha256"], uri="paint-rgba.png",
                                     alpha="premultiplied", channels=("red", "green", "blue", "alpha"))
        unsupported = convert_texture_to_materialx(LIBRARY, premultiplied, self.stdlib())
        self.assertEqual(unsupported.original, png)
        self.assertIsNone(unsupported.converted)
        self.assertIn("unsupported_texture_layout", self.codes(unsupported))
        wrong_metadata = self.binding(original=png, sha256=metadata["sha256"], uri="paint-rgba.png")
        rejected = convert_texture_to_materialx(LIBRARY, wrong_metadata, self.stdlib())
        self.assertIsNone(rejected.converted)
        self.assertIn("unsupported_texture_layout", self.codes(rejected))
        rgba16 = png[:24] + b"\x10" + png[25:]
        wrong_16bit = self.binding(original=rgba16, sha256=hashlib.sha256(rgba16).hexdigest(), uri="rgba16.png")
        rejected_16bit = convert_texture_to_materialx(LIBRARY, wrong_16bit, self.stdlib())
        self.assertIsNone(rejected_16bit.converted)
        self.assertIn("unsupported_texture_layout", self.codes(rejected_16bit))
        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", binascii.crc32(kind + data) & 0xffffffff)
        rgb_transparent = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0))
                           + chunk(b"tRNS", b"\0\xff\0\0\0\0") + chunk(b"IDAT", zlib.compress(b"\0\xff\0\0"))
                           + chunk(b"IEND", b""))
        wrong_trns = self.binding(original=rgb_transparent, sha256=hashlib.sha256(rgb_transparent).hexdigest(),
                                  uri="transparent-rgb.png")
        rejected_trns = convert_texture_to_materialx(LIBRARY, wrong_trns, self.stdlib())
        self.assertIsNone(rejected_trns.converted)
        self.assertIn("unsupported_texture_layout", self.codes(rejected_trns))
        truncated = png[:20]
        wrong_truncated = self.binding(original=truncated, sha256=hashlib.sha256(truncated).hexdigest(),
                                       uri="truncated.png")
        rejected_truncated = convert_texture_to_materialx(LIBRARY, wrong_truncated, self.stdlib())
        self.assertIsNone(rejected_truncated.converted)
        self.assertIn("unsupported_texture_layout", self.codes(rejected_truncated))
        result = convert_texture_to_materialx(LIBRARY, binding, self.stdlib())
        self.assertEqual(result.original, png)
        self.assertEqual((result.texture.alpha, result.texture.channels), (binding.alpha, binding.channels))
        if ENABLED == "1":
            self.assertIsNotNone(result.converted)
            image = ET.fromstring(result.converted).find("./image[@name='paint_color']")
            self.assertEqual(image.get("type"), "color4")
            self.assertIn("alpha_association_external", self.codes(result))
        else:
            self.assertIn("backend_unavailable", self.codes(result))

    def test_host_can_select_a_replacement_converter(self):
        binding = self.binding()
        class Alternate:
            def execute(self, texture):
                metadata = TextureMetadata(texture.sha256, texture.uri, texture.color_space, texture.working_color_space,
                                           texture.alpha, texture.channels, texture.semantic, texture.node_name,
                                           texture.u_address, texture.v_address, texture.filtering)
                return MaterialExchangeResult(texture.original, (), True, False, False, "", (), (),
                                              b"alternate:" + texture.original, metadata)
        def execute_selected(converter, texture):
            return converter.execute(texture)
        alternate = execute_selected(Alternate(), binding)
        self.assertEqual(alternate.converted, b"alternate:" + self.texture)
        self.assertEqual(alternate.texture.channels, binding.channels)
        actual = execute_selected(MaterialXTextureConverter(LIBRARY, self.stdlib()), binding)
        self.assertEqual(actual.original, self.texture)
        if ENABLED == "1":
            self.assertIsNotNone(actual.converted)
        else:
            self.assertIn("backend_unavailable", self.codes(actual))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
