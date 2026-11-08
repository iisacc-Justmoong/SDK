"""Python access to real iiGPU devices, buffers and bounded transfers."""
import ctypes

UNAVAILABLE = 2
UNSUPPORTED = 3
LIMIT_EXCEEDED = 7


class ResourceError(RuntimeError):
    def __init__(self, code):
        self.code = code
        super().__init__(f"iiGPU resource error: {code}")


def _number(value):
    if not isinstance(value, int) or value < 0 or value > (1 << 64) - 1:
        raise ValueError("value must fit an unsigned 64-bit integer")
    return value


class Library:
    def __init__(self, path):
        self._dll = ctypes.CDLL(str(path))
        p, u, n = ctypes.c_void_p, ctypes.c_uint64, ctypes.c_size_t
        po, uo = ctypes.POINTER(p), ctypes.POINTER(u)
        signatures = {
            "device_create": ([ctypes.c_int, u, po], ctypes.c_int),
            "device_destroy": ([p], None),
            "device_budget": ([p, uo], ctypes.c_int),
            "device_set_budget": ([p, u], ctypes.c_int),
            "device_allocated": ([p, uo], ctypes.c_int),
            "buffer_create": ([p, u, ctypes.c_int, po], ctypes.c_int),
            "buffer_destroy": ([p], None),
            "buffer_size": ([p, uo], ctypes.c_int),
            "buffer_read": ([p, u, p, n], ctypes.c_int),
            "buffer_write": ([p, u, p, n], ctypes.c_int),
            "buffer_transfer": ([p, p, ctypes.c_int, ctypes.c_int, u, po], ctypes.c_int),
        }
        self._functions = {}
        for name, (arguments, result) in signatures.items():
            function = getattr(self._dll, "iigpu_resource_" + name)
            function.argtypes, function.restype = arguments, result
            self._functions[name] = function

    def _call(self, name, *arguments):
        status = self._functions[name](*arguments)
        if status:
            raise ResourceError(status)

    def device(self, backend="automatic", memory_budget=256 * 1024 * 1024):
        backends = {"automatic": 0, "metal": 1, "vulkan": 2}
        if backend not in backends:
            raise ValueError("unknown GPU backend")
        result = ctypes.c_void_p()
        self._call("device_create", backends[backend], _number(memory_budget), ctypes.byref(result))
        return Device(self, result)


class _Owner:
    def __init__(self, library, handle):
        self._library, self._handle = library, handle

    @property
    def closed(self):
        return not self._handle or not self._handle.value

    def _get(self):
        if self.closed:
            raise ResourceError(5)
        return self._handle

    def close(self):
        if not self.closed:
            handle, self._handle = self._handle, None
            self._library._functions[self._destroy](handle)

    def __enter__(self):
        self._get()
        return self

    def __exit__(self, *_):
        self.close()

    def __del__(self):
        try:
            if getattr(self, "_handle", None):
                self.close()
        except Exception:
            pass


class Device(_Owner):
    _destroy = "device_destroy"

    def _get_number(self, name):
        value = ctypes.c_uint64()
        self._library._call(name, self._get(), ctypes.byref(value))
        return value.value

    @property
    def memory_budget(self):
        return self._get_number("device_budget")

    @memory_budget.setter
    def memory_budget(self, value):
        self._library._call("device_set_budget", self._get(), _number(value))

    @property
    def allocated_bytes(self):
        return self._get_number("device_allocated")

    def buffer(self, size, device_memory=False):
        handle = ctypes.c_void_p()
        self._library._call("buffer_create", self._get(), _number(size), int(bool(device_memory)), ctypes.byref(handle))
        return Buffer(self._library, handle)


class Buffer(_Owner):
    _destroy = "buffer_destroy"

    @property
    def size(self):
        value = ctypes.c_uint64()
        self._library._call("buffer_size", self._get(), ctypes.byref(value))
        return value.value

    def read(self, size=None, offset=0):
        offset = _number(offset)
        available = self.size
        if offset > available:
            raise ValueError("offset exceeds buffer")
        size = available - offset if size is None else _number(size)
        if size > available - offset:
            raise ValueError("read exceeds buffer")
        data = ctypes.create_string_buffer(size)
        self._library._call("buffer_read", self._get(), offset, data, size)
        return data.raw

    def write(self, data, offset=0):
        data, offset = bytes(data), _number(offset)
        available = self.size
        if offset > available or len(data) > available - offset:
            raise ValueError("write exceeds buffer")
        source = ctypes.create_string_buffer(data, len(data))
        self._library._call("buffer_write", self._get(), offset, source, len(data))

    def _transfer(self, destination, move, device_memory, chunk_bytes):
        if not isinstance(destination, Device) or destination._library is not self._library:
            raise ValueError("destination must belong to the same library")
        result = ctypes.c_void_p()
        self._library._call("buffer_transfer", destination._get(), self._get(), int(move),
                            int(bool(device_memory)), _number(chunk_bytes), ctypes.byref(result))
        if move:
            self.close()
        return Buffer(self._library, result)

    def copy_to(self, destination, device_memory=False, chunk_bytes=1024 * 1024):
        return self._transfer(destination, False, device_memory, chunk_bytes)

    def move_to(self, destination, device_memory=False, chunk_bytes=1024 * 1024):
        return self._transfer(destination, True, device_memory, chunk_bytes)
