"""Exercise Python compute marshalling without initializing a physical GPU."""
import ctypes
import importlib.util
import os
from pathlib import Path
import threading
import unittest
from unittest import mock

path = Path(__file__).resolve().parents[1] / "src/python/iigpu_resources.py"
spec = importlib.util.spec_from_file_location("iigpu_resources", path)
gpu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gpu)


class FakeBridge:
    def __init__(self):
        self.destroyed = 0
        self.status = 0
        self.calls = []
        self.entered = None
        self.release = None

    def create(self, backend, budget, maximum, chunk, out):
        self.calls.append((backend, budget, maximum, chunk))
        ctypes.cast(out, ctypes.POINTER(ctypes.c_void_p))[0] = ctypes.c_void_p(123)
        return 0

    def destroy(self, _handle):
        self.destroyed += 1

    def count(self, _handle, out):
        ctypes.cast(out, ctypes.POINTER(ctypes.c_size_t))[0] = 2
        return 0

    def execute(self, _handle, program, source, source_size, in_stride, target, target_size, out_stride):
        p = ctypes.cast(program, ctypes.POINTER(gpu._ComputeProgram)).contents
        self.calls.append((p.metal_source, ctypes.string_at(p.spirv, p.spirv_size),
                           ctypes.string_at(p.constants, p.constants_size), p.group_size_x,
                           ctypes.string_at(source, source_size), in_stride, out_stride))
        if self.entered is not None:
            self.entered.set()
            if not self.release.wait(5):
                return 2
        if self.status:
            return self.status
        ctypes.memmove(target, b"computed"[:target_size], target_size)
        return 0

    def library(self):
        library = object.__new__(gpu.Library)
        library._functions = {"manager_create": self.create, "manager_destroy": self.destroy,
                              "manager_count": self.count, "manager_execute": self.execute}
        return library


class ComputePythonTests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("IIGPU_COMPUTE_LIBRARY"), "shared C bridge was not supplied")
    def test_real_shared_bridge_loads_and_rejects_invalid_manager(self):
        library = gpu.Library(os.environ["IIGPU_COMPUTE_LIBRARY"])
        handle = ctypes.c_void_p(123)
        with self.assertRaises(gpu.ResourceError) as error:
            library._call("manager_create", 42, 1024, 1, 1, ctypes.byref(handle))
        self.assertEqual(error.exception.code, 1)
        self.assertIsNone(handle.value)

    def test_program_marshalling_and_lifetime(self):
        fake = FakeBridge()
        library = fake.library()
        with library.compute_manager(backend="vulkan", memory_budget=1024, max_devices=2,
                                     max_items_per_chunk=3) as manager:
            self.assertEqual(manager.device_count, 2)
            output = manager.execute(b"abcd", input_stride=2, output_stride=4,
                                     metal_source="kernel source", spirv=b"\x03\x02\x23\x07",
                                     constants=b"\x01\x00\x00\x00", group_size=32)
            self.assertEqual(output, b"computed")
        self.assertEqual(fake.calls[0], (2, 1024, 2, 3))
        self.assertEqual(fake.calls[1], (b"kernel source", b"\x03\x02\x23\x07",
                                          b"\x01\x00\x00\x00", 32, b"abcd", 2, 4))
        self.assertEqual(fake.destroyed, 1)
        manager.close()
        self.assertEqual(fake.destroyed, 1)
        with self.assertRaises(gpu.ResourceError):
            manager.execute(b"abcd", input_stride=2, output_stride=4, metal_source="x")

    def test_rejects_invalid_input_and_propagates_native_failure(self):
        fake = FakeBridge()
        with fake.library().compute_manager() as manager:
            for args in [dict(input_stride=0, output_stride=1),
                         dict(input_stride=3, output_stride=1),
                         dict(input_stride=2, output_stride=0),
                         dict(input_stride=2, output_stride=1, constants=b"x"),
                         dict(input_stride=2, output_stride=1, spirv=b"x"),
                         dict(input_stride=2, output_stride=1, group_size=0)]:
                with self.subTest(args=args), self.assertRaises(ValueError):
                    manager.execute(b"abcd", metal_source="x", **args)
            fake.status = 2
            with self.assertRaises(gpu.ResourceError) as error:
                manager.execute(b"abcd", input_stride=2, output_stride=1, metal_source="x")
            self.assertEqual(error.exception.code, 2)

    def test_old_bridge_reports_managed_compute_as_unsupported(self):
        class Function:
            def __call__(self, *_args):
                return 0
        class OldBridge:
            def __getattr__(self, name):
                if name.startswith("iigpu_resource_manager_"):
                    raise AttributeError(name)
                return Function()
        with mock.patch.object(gpu.ctypes, "CDLL", return_value=OldBridge()):
            library = gpu.Library("old-bridge")
        self.assertIn("device_create", library._functions)
        with self.assertRaises(gpu.ResourceError) as error:
            library.compute_manager()
        self.assertEqual(error.exception.code, gpu.UNSUPPORTED)

    def test_close_waits_for_inflight_python_call(self):
        fake = FakeBridge()
        fake.entered, fake.release = threading.Event(), threading.Event()
        manager = fake.library().compute_manager()
        values, finished = [], threading.Event()
        worker = threading.Thread(target=lambda: values.append(
            manager.execute(b"abcd", input_stride=2, output_stride=4, metal_source="x")))
        worker.start()
        self.assertTrue(fake.entered.wait(5))
        closer = threading.Thread(target=lambda: (manager.close(), finished.set()))
        closer.start()
        try:
            self.assertFalse(finished.wait(0.05))
            self.assertEqual(fake.destroyed, 0)
        finally:
            fake.release.set()
            worker.join(5)
            closer.join(5)
        self.assertFalse(worker.is_alive() or closer.is_alive())
        self.assertEqual(values, [b"computed"])
        self.assertEqual(fake.destroyed, 1)


if __name__ == "__main__":
    unittest.main()
