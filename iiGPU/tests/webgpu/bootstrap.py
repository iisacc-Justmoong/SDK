"""Offline tests for safe, reproducible native dependency acquisition."""
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import unittest
from unittest import mock
import resource_workspace

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("fetch_dawn", ROOT / "tools/fetch_dawn.py")
fetch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fetch)


class BootstrapTests(unittest.TestCase):
    def setUp(self):
        self.temp = resource_workspace.temporary(ROOT, "webgpu-bootstrap-")
        self.root = Path(self.temp.name)
        self.storage = mock.MagicMock()

    def tearDown(self):
        self.temp.cleanup()

    def test_exact_platform_selection(self):
        self.assertEqual(fetch.host_key("Darwin", "arm64"), "macos-arm64")
        self.assertEqual(fetch.host_key("Windows", "AMD64"), "windows-x64")
        self.assertEqual(fetch.host_key("Linux", "aarch64"), "linux-arm64")
        with self.assertRaises(ValueError):
            fetch.host_key("Unknown", "x86_64")

    def test_default_destinations_do_not_mix_target_binaries(self):
        self.assertEqual(fetch.default_destination("macos-arm64"), ROOT / "resources/dawn/macos-arm64")
        self.assertEqual(fetch.default_destination("windows-x64"), ROOT / "resources/dawn/windows-x64")
        self.assertNotEqual(fetch.default_destination("macos-arm64"), fetch.default_destination("windows-x64"))

    def test_changed_dependency_is_preserved(self):
        p = mock.MagicMock()
        p.exists.side_effect = (False, True, True)
        p.read_bytes.side_effect = (b"one", b"one")
        fetch.put_verified(p, b"one", storage=self.storage)
        fetch.put_verified(p, b"one", storage=self.storage)
        with self.assertRaises(ValueError):
            fetch.put_verified(p, b"two", storage=self.storage)
        self.storage.mkdir.assert_called_once()
        self.storage.open.assert_called_once()

    def test_storage_is_explicitly_supplied(self):
        self.assertFalse(hasattr(fetch, "provider"))
        with self.assertRaises(TypeError):
            fetch.put_verified(self.root / "data", b"one")

    def test_cached_archive_must_match_lock(self):
        p = self.root / "archive"
        p.write_bytes(b"corrupt")
        with self.assertRaises(ValueError):
            fetch.checked_download("https://invalid.example/unused", fetch.digest(b"correct"), p, storage=self.storage)

    def test_archive_cannot_escape_destination(self):
        for name, symlink in (("root/../../escaped", False), ("/absolute", False), ("root/unsafe", True)):
            with self.subTest(name=name):
                p = self.root / "bad.tar.gz"
                with tarfile.open(p,"w:gz") as archive:
                    info = tarfile.TarInfo(name)
                    if symlink:
                        info.type = tarfile.SYMTYPE
                        info.linkname = "../../escaped"
                        archive.addfile(info)
                    else:
                        info.size = 1
                        archive.addfile(info, io.BytesIO(b"x"))
                with self.assertRaises(ValueError):
                    fetch.extract(p, self.root / "dest", storage=self.storage)
        self.assertFalse((self.root.parent / "escaped").exists())

    def test_lock_has_exact_hashes_and_original_licenses(self):
        lock=json.loads((ROOT / "tools/dawn.lock.json").read_text())
        self.assertEqual(len(lock["revision"]),40)
        self.assertEqual(set(lock["assets"]),{"macos-arm64","macos-x64","linux-x64","windows-x64"})
        self.assertGreaterEqual(len(lock["licenses"]),4)
        for entry in list(lock["assets"].values()) + lock["licenses"]:
            self.assertRegex(entry["sha256"],r"^[0-9a-f]{64}$")
            self.assertTrue(entry["url"].startswith("https://"))


if __name__ == "__main__":
    unittest.main()
