"""Explicit-path Python binding for the iiPBRMaterial MaterialX inspection slice.

The caller supplies both the native library path and all document bytes. Results
are copied before the native allocation is freed; this module does not open
MaterialX documents or discover node libraries on the caller's behalf.
"""

from dataclasses import dataclass
import ctypes
import json
import os
from typing import Optional, Protocol, Sequence


@dataclass(frozen=True)
class LibraryDocument:
    """Caller-owned definition bytes and an explicitly declared identity."""
    id: str
    version: str
    sha256: str
    xml: bytes


@dataclass(frozen=True)
class LibraryIdentity:
    """Declared library identity; SHA-256 content verification is not implied."""
    id: str
    version: str
    sha256: str


@dataclass(frozen=True)
class MaterialNode:
    """Resolved node metadata, without a promise of rendering support."""
    path: str
    category: str
    type: str
    requested_version: str
    definition: str
    resolved_version: str
    library_id: str
    library_version: str
    library_sha256: str


@dataclass(frozen=True)
class Diagnostic:
    """A stable diagnostic code and its severity, location and explanation."""
    code: str
    severity: str
    location: str
    message: str


@dataclass(frozen=True)
class TextureBinding:
    """Texture bytes and explicit color, alpha, channels and sampler semantics."""
    original: bytes
    sha256: str
    uri: str
    color_space: str
    working_color_space: str
    alpha: str
    channels: tuple[str, ...]
    semantic: str
    node_name: str
    u_address: str
    v_address: str
    filtering: str


@dataclass(frozen=True)
class TextureMetadata:
    """Owned texture interpretation returned even when conversion is rejected."""
    sha256: str
    uri: str
    color_space: str
    working_color_space: str
    alpha: str
    channels: tuple[str, ...]
    semantic: str
    node_name: str
    u_address: str
    v_address: str
    filtering: str


@dataclass(frozen=True)
class MaterialExchangeResult:
    """Owned native result, including the original even when conversion fails."""
    original: bytes
    libraries: tuple[LibraryIdentity, ...]
    backend_available: bool
    parsed: bool
    native_valid: bool
    document_version: str
    nodes: tuple[MaterialNode, ...]
    diagnostics: tuple[Diagnostic, ...]
    converted: Optional[bytes]
    texture: Optional[TextureMetadata] = None


class _Bytes(ctypes.Structure):
    _fields_ = [("data", ctypes.c_void_p), ("size", ctypes.c_size_t)]


class _Library(ctypes.Structure):
    _fields_ = [("id", _Bytes), ("version", _Bytes), ("sha256", _Bytes), ("xml", _Bytes)]


class _Request(ctypes.Structure):
    _fields_ = [("original", _Bytes), ("libraries", ctypes.POINTER(_Library)),
                ("library_count", ctypes.c_size_t), ("request_conversion", ctypes.c_int)]


class _TextureBinding(ctypes.Structure):
    _fields_ = [("original", _Bytes), ("sha256", _Bytes), ("uri", _Bytes), ("color_space", _Bytes),
                ("working_color_space", _Bytes), ("alpha", _Bytes), ("channels", ctypes.POINTER(_Bytes)),
                ("channel_count", ctypes.c_size_t), ("semantic", _Bytes), ("node_name", _Bytes),
                ("u_address", _Bytes), ("v_address", _Bytes), ("filtering", _Bytes),
                ("libraries", ctypes.POINTER(_Library)), ("library_count", ctypes.c_size_t)]


def _load(native_library: os.PathLike | str):
    """Load only the path selected by the caller; no global library cache."""
    binary = ctypes.CDLL(os.fspath(native_library))
    binary.ii_pbr_exchange_free.argtypes = [ctypes.c_void_p]
    binary.ii_pbr_exchange_free.restype = None
    return binary


def _borrow(data: bytes, keepalive: list) -> _Bytes:
    """Keep each native byte range valid until its synchronous call returns."""
    data = bytes(data)
    buffer = ctypes.create_string_buffer(data)
    keepalive.append(buffer)
    return _Bytes(ctypes.addressof(buffer), len(data))


def _libraries(libraries: Sequence[LibraryDocument], keepalive: list):
    """Copy explicitly supplied library inputs into temporary C ABI views."""
    native_libraries = (_Library * len(libraries))()
    for index, library in enumerate(libraries):
        native_libraries[index] = _Library(_borrow(library.id.encode("utf-8"), keepalive),
                                           _borrow(library.version.encode("utf-8"), keepalive),
                                           _borrow(library.sha256.encode("utf-8"), keepalive),
                                           _borrow(library.xml, keepalive))
    return native_libraries


def _owned_text(binary, pointer) -> str:
    """Copy and free a single allocation even if decoding raises an error."""
    if not pointer:
        raise MemoryError("The native exchange operation could not allocate its result")
    try:
        return ctypes.string_at(pointer).decode("ascii")
    finally:
        binary.ii_pbr_exchange_free(pointer)


def _decode_result(binary, pointer) -> MaterialExchangeResult:
    """Decode an owned native result, preserving binary inputs exactly."""
    raw = json.loads(_owned_text(binary, pointer))
    if raw.get("schema") != 1:
        raise RuntimeError("Unsupported native exchange result schema")

    def text(value: str) -> str:
        return bytes.fromhex(value).decode("utf-8", errors="surrogateescape")

    texture = raw.get("texture")
    if texture is not None:
        texture = TextureMetadata(**{key: tuple(text(v) for v in value) if key == "channels" else text(value)
                                    for key, value in texture.items()})

    return MaterialExchangeResult(
        original=bytes.fromhex(raw["original"]),
        libraries=tuple(LibraryIdentity(**{key: text(value) for key, value in item.items()})
                        for item in raw["libraries"]),
        backend_available=raw["backend_available"], parsed=raw["parsed"], native_valid=raw["native_valid"],
        document_version=text(raw["document_version"]),
        nodes=tuple(MaterialNode(**{key: text(value) for key, value in item.items()}) for item in raw["nodes"]),
        diagnostics=tuple(Diagnostic(**{key: text(value) for key, value in item.items()})
                          for item in raw["diagnostics"]),
        converted=None if raw["converted"] is None else bytes.fromhex(raw["converted"]),
        texture=texture,
    )


def _inspect(native_library, original, libraries, convert, strict):
    binary = _load(native_library)
    operation = binary.ii_pbr_exchange_inspect_strict if strict else binary.ii_pbr_exchange_inspect
    operation.argtypes = [ctypes.POINTER(_Request)]
    operation.restype = ctypes.c_void_p
    keepalive = []
    native_libraries = _libraries(libraries, keepalive)
    request = _Request(_borrow(original, keepalive), native_libraries, len(libraries), int(convert))
    return _decode_result(binary, operation(ctypes.byref(request)))


def inspect_materialx(native_library: os.PathLike | str, original: bytes,
                      libraries: Sequence[LibraryDocument], *, convert: bool = False) -> MaterialExchangeResult:
    """Compatibility inspection; library hashes are retained but not verified."""
    return _inspect(native_library, original, libraries, convert, False)


def inspect_materialx_strict(native_library: os.PathLike | str, original: bytes,
                             libraries: Sequence[LibraryDocument]) -> MaterialExchangeResult:
    """Verify library hashes and reject ambiguous XML before native inspection."""
    return _inspect(native_library, original, libraries, False, True)


def sha256_bytes(native_library: os.PathLike | str, data: bytes) -> str:
    """Return native SHA-256 for exactly the caller's bytes, with no file access."""
    binary = _load(native_library)
    operation = binary.ii_pbr_exchange_sha256
    operation.argtypes = [_Bytes]
    operation.restype = ctypes.c_void_p
    keepalive = []
    return _owned_text(binary, operation(_borrow(data, keepalive)))


def convert_texture_to_materialx(native_library: os.PathLike | str, texture: TextureBinding,
                                 libraries: Sequence[LibraryDocument]) -> MaterialExchangeResult:
    """Create an RGB or straight RGBA8 PNG image node, retaining bytes and metadata."""
    binary = _load(native_library)
    operation = binary.ii_pbr_exchange_convert_texture
    operation.argtypes = [ctypes.POINTER(_TextureBinding)]
    operation.restype = ctypes.c_void_p
    keepalive = []
    native_libraries = _libraries(libraries, keepalive)
    channels = (_Bytes * len(texture.channels))(*[_borrow(item.encode("utf-8"), keepalive)
                                                   for item in texture.channels])
    def encode(value):
        return _borrow(value.encode("utf-8"), keepalive)
    request = _TextureBinding(_borrow(texture.original, keepalive), encode(texture.sha256), encode(texture.uri),
                              encode(texture.color_space), encode(texture.working_color_space), encode(texture.alpha),
                              channels, len(texture.channels), encode(texture.semantic), encode(texture.node_name),
                              encode(texture.u_address), encode(texture.v_address), encode(texture.filtering),
                              native_libraries, len(libraries))
    return _decode_result(binary, operation(ctypes.byref(request)))


class TextureConverter(Protocol):
    """A host-selected converter that returns the same owned result contract."""
    def execute(self, texture: TextureBinding) -> MaterialExchangeResult: ...


class MaterialXTextureConverter:
    """Adapter with an explicit native binary and immutable node-library snapshot."""
    def __init__(self, native_library: os.PathLike | str, libraries: Sequence[LibraryDocument]):
        self._native_library = os.fspath(native_library)
        self._libraries = tuple(libraries)

    def execute(self, texture: TextureBinding) -> MaterialExchangeResult:
        """Convert once without acquiring the caller's file or texture lifetime."""
        return convert_texture_to_materialx(self._native_library, texture, self._libraries)
