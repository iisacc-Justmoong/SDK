"""Python access to real iiGPU devices, buffers and bounded transfers."""
import ctypes
import sys
import threading

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


def _positive(value, maximum):
    value = _number(value)
    if not value or value > maximum:
        raise ValueError("value must be positive and within the supported range")
    return value


class _ComputeProgram(ctypes.Structure):
    _fields_ = [("metal_source", ctypes.c_char_p), ("metal_entry", ctypes.c_char_p),
                ("metal_library", ctypes.c_void_p), ("metal_library_size", ctypes.c_size_t),
                ("spirv", ctypes.c_void_p), ("spirv_size", ctypes.c_size_t),
                ("vulkan_entry", ctypes.c_char_p), ("group_size_x", ctypes.c_uint32),
                ("constants", ctypes.c_void_p), ("constants_size", ctypes.c_size_t)]


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
            "manager_create": ([ctypes.c_int, u, n, ctypes.c_uint32, po], ctypes.c_int),
            "manager_destroy": ([p], None),
            "manager_count": ([p, ctypes.POINTER(n)], ctypes.c_int),
            "manager_execute": ([p, ctypes.POINTER(_ComputeProgram), p, n, n, p, n, n], ctypes.c_int),
        }
        self._functions = {}
        for name, (arguments, result) in signatures.items():
            try:
                function = getattr(self._dll, "iigpu_resource_" + name)
            except AttributeError:
                if name.startswith("manager_"):
                    continue
                raise
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

    def compute_manager(self, backend="automatic", memory_budget=256 * 1024 * 1024,
                        max_devices=8, max_items_per_chunk=65536):
        if any(name not in self._functions for name in
               ("manager_create", "manager_destroy", "manager_count", "manager_execute")):
            raise ResourceError(UNSUPPORTED)
        backends = {"automatic": 0, "metal": 1, "vulkan": 2}
        if backend not in backends:
            raise ValueError("unknown GPU backend")
        size_max = ctypes.c_size_t(-1).value
        result = ctypes.c_void_p()
        self._call("manager_create", backends[backend], _positive(memory_budget, (1 << 64) - 1),
                   _positive(max_devices, size_max), _positive(max_items_per_chunk, (1 << 32) - 1),
                   ctypes.byref(result))
        return ComputeManager(self, result)


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


class ComputeManager(_Owner):
    _destroy = "manager_destroy"

    def __init__(self, library, handle):
        super().__init__(library, handle)
        self._operation = threading.RLock()

    def close(self):
        with self._operation:
            super().close()

    @property
    def device_count(self):
        with self._operation:
            value = ctypes.c_size_t()
            self._library._call("manager_count", self._get(), ctypes.byref(value))
            return value.value

    def execute(self, data, *, input_stride, output_stride, metal_source="", metal_library=b"",
                spirv=b"", metal_entry="main0", vulkan_entry="main", group_size=64, constants=b""):
        data, metal_library, spirv, constants = bytes(data), bytes(metal_library), bytes(spirv), bytes(constants)
        size_max = ctypes.c_size_t(-1).value
        input_stride = _positive(input_stride, size_max)
        output_stride = _positive(output_stride, size_max)
        group_size = _positive(group_size, (1 << 32) - 1)
        if len(data) % input_stride or len(spirv) % 4 or len(constants) % 4:
            raise ValueError("input elements, SPIR-V and constants must have valid alignment")
        if len(constants) > (1 << 32) - 13:
            raise ValueError("constants exceed the supported size")
        output_size = len(data) // input_stride * output_stride
        if output_size > min(size_max, sys.maxsize):
            raise ValueError("output exceeds the supported size")
        for text in (metal_source, metal_entry, vulkan_entry):
            if not isinstance(text, str) or "\0" in text:
                raise ValueError("shader source and entry points must be NUL-free strings")
        if not metal_entry or not vulkan_entry:
            raise ValueError("shader entry points must be nonempty")
        source_text, metal_name, vulkan_name = (s.encode("utf-8") for s in (metal_source, metal_entry, vulkan_entry))
        def storage(value):
            return ctypes.create_string_buffer(value, len(value)) if value else None
        source_buffer, metal_buffer, spirv_buffer, constants_buffer = map(storage, (data, metal_library, spirv, constants))
        output = ctypes.create_string_buffer(output_size)
        program = _ComputeProgram(source_text, metal_name, ctypes.cast(metal_buffer, ctypes.c_void_p), len(metal_library),
                                  ctypes.cast(spirv_buffer, ctypes.c_void_p), len(spirv), vulkan_name, group_size,
                                  ctypes.cast(constants_buffer, ctypes.c_void_p), len(constants))
        with self._operation:
            self._library._call("manager_execute", self._get(), ctypes.byref(program), source_buffer, len(data),
                                input_stride, output, output_size, output_stride)
        return output.raw


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
