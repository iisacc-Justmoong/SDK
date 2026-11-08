"""Exercise the Python bridge against an actual hardware GPU when available."""
import importlib.util
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("iigpu_resources", root / "src/python/iigpu_resources.py")
gpu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gpu)
api = gpu.Library(sys.argv[1])
try:
    first = api.device(sys.argv[2], 1024 * 1024)
    second = api.device(sys.argv[2], 1024 * 1024)
except gpu.ResourceError as error:
    if error.code in (gpu.UNAVAILABLE, gpu.UNSUPPORTED):
        print("SKIP hardware GPU unavailable")
        sys.exit(77)
    raise

with first, second:
    assert first.memory_budget == 1024 * 1024
    with first.buffer(16) as source:
        assert source.read() == bytes(16)
        source.write(b"device migration")
        with source.move_to(second, device_memory=True, chunk_bytes=8) as moved:
            assert source.closed
            with moved.copy_to(second, device_memory=False, chunk_bytes=8) as returned:
                assert returned.read() == b"device migration"
    assert first.allocated_bytes == 0
    first.memory_budget = 0
    try:
        first.buffer(4)
        raise AssertionError("zero budget allowed GPU allocation")
    except gpu.ResourceError as error:
        assert error.code == gpu.LIMIT_EXCEEDED
print("GPU_RESOURCE_PYTHON_OK")
