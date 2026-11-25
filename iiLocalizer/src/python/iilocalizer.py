"""Python access to iiLocalizer's native UTF-8 PO catalog and text substitution."""

from __future__ import annotations

import ctypes
import ctypes.util
import os
from dataclasses import dataclass
from pathlib import Path
import sys
import threading


class LocalizationError(ValueError):
    """The native PO reader or message formatter rejected the supplied data."""


@dataclass(frozen=True)
class Translation:
    """The final text and its origin: translated, source_missing or source_invalid_translation."""

    text: str
    origin: str


@dataclass(frozen=True)
class LabelUsage:
    """An application-owned location, original message and distinct string key."""

    location: str
    source: str
    key: str


class _NativeLabelUsage(ctypes.Structure):
    """Mirror the public, length-delimited C ABI label-usage record."""

    _fields_ = [
        ('location', ctypes.c_char_p), ('location_length', ctypes.c_size_t),
        ('source', ctypes.c_char_p), ('source_length', ctypes.c_size_t),
        ('key', ctypes.c_char_p), ('key_length', ctypes.c_size_t),
    ]


def _library():
    """Locate the explicit native library first, then an installed sibling or system library."""
    explicit = os.environ.get('IILOCALIZER_LIBRARY')
    if explicit:
        return ctypes.CDLL(explicit)
    suffix = '.dll' if sys.platform == 'win32' else '.dylib' if sys.platform == 'darwin' else '.so'
    name = ('iiLocalizerC' if sys.platform == 'win32' else 'libiiLocalizerC') + suffix
    sibling = Path(__file__).resolve().parent.parent / name
    if sibling.exists():
        return ctypes.CDLL(str(sibling))
    if sys.platform == 'win32':
        binary = Path(__file__).resolve().parent.parent.parent / 'bin' / name
        if binary.exists():
            return ctypes.CDLL(str(binary))
    found = ctypes.util.find_library('iiLocalizerC')
    if found:
        return ctypes.CDLL(found)
    raise OSError('iiLocalizer native library was not found; set IILOCALIZER_LIBRARY')


_lib = _library()
_void = ctypes.c_void_p
_char_pp = ctypes.POINTER(ctypes.c_void_p)
_lib.iil_catalog_create.argtypes = [ctypes.c_char_p, ctypes.c_size_t, _char_pp, _char_pp]
_lib.iil_catalog_create.restype = ctypes.c_int
_lib.iil_catalog_destroy.argtypes = [_void]
_lib.iil_catalog_destroy.restype = None
_lib.iil_catalog_language.argtypes = [_void, _char_pp, _char_pp]
_lib.iil_catalog_language.restype = ctypes.c_int
_lib.iil_catalog_translate.argtypes = [
    _void, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_int,
    ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_size_t), ctypes.c_size_t,
    _char_pp, ctypes.POINTER(ctypes.c_int), _char_pp,
]
_lib.iil_catalog_translate.restype = ctypes.c_int
_indices = ctypes.POINTER(ctypes.c_size_t)
_lib.iil_select_label_usages.argtypes = [
    ctypes.POINTER(_NativeLabelUsage), ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t,
    ctypes.c_char_p, ctypes.c_size_t, ctypes.c_int, ctypes.POINTER(_indices),
    ctypes.POINTER(ctypes.c_size_t), _char_pp,
]
_lib.iil_select_label_usages.restype = ctypes.c_int
_lib.iil_indices_free.argtypes = [_indices]
_lib.iil_indices_free.restype = None
_lib.iil_string_free.argtypes = [_void]
_lib.iil_string_free.restype = None


def _take(pointer):
    """Decode and release one independently allocated native string."""
    if not pointer.value:
        return ''
    try:
        return ctypes.string_at(pointer.value).decode('utf-8')
    finally:
        _lib.iil_string_free(pointer)


def _check(code, error):
    """Raise a stable Python exception for one failed C ABI operation."""
    if code:
        raise LocalizationError(_take(error) or 'iiLocalizer operation failed')
    if error.value:
        _take(error)


def _encode(value, description):
    """Accept strings only and encode them without implicit locale conversions."""
    if not isinstance(value, str):
        raise TypeError(f'{description} must be a string')
    return value.encode('utf-8')


def select_label_usages(usages, source, location=None):
    """Return registered indices for source; omit location to select all matching uses.

    A supplied location must match exactly. An unknown location returns an empty
    list, while empty or duplicate locations and shared keys raise LocalizationError.
    """
    source_bytes = _encode(source, 'source')
    location_bytes = _encode(location, 'location') if location is not None else b''
    field_limit, registry_limit = 65536, 8 * 1024 * 1024
    if len(source_bytes) > field_limit or len(location_bytes) > field_limit:
        raise LocalizationError('Label selection exceeds its size limit')
    total = len(source_bytes) + len(location_bytes)
    encoded = []
    for item in usages:
        if len(encoded) >= 10000:
            raise LocalizationError('Too many label usages')
        if not isinstance(item, LabelUsage):
            raise TypeError('usages must contain LabelUsage values')
        fields = (_encode(item.location, 'usage location'),
                  _encode(item.source, 'usage source'), _encode(item.key, 'usage key'))
        for field in fields:
            if len(field) > field_limit or len(field) > registry_limit - total:
                raise LocalizationError('Label usage registry exceeds its size limit')
            total += len(field)
        encoded.append(fields)
    entries = (_NativeLabelUsage * len(encoded))(*(
        _NativeLabelUsage(place, len(place), message, len(message), key, len(key))
        for place, message, key in encoded
    ))
    indices, count, error = _indices(), ctypes.c_size_t(), _void()
    code = _lib.iil_select_label_usages(
        entries, len(encoded), source_bytes, len(source_bytes), location_bytes, len(location_bytes),
        location is not None, ctypes.byref(indices), ctypes.byref(count), ctypes.byref(error))
    try:
        _check(code, error)
        return [indices[i] for i in range(count.value)]
    finally:
        if indices:
            _lib.iil_indices_free(indices)


class Catalog:
    """Own an immutable native PO catalog and serialize close against active calls."""

    def __init__(self, handle):
        """Wrap a handle created by from_po; callers should use the factory."""
        self._handle = handle
        self._lock = threading.RLock()

    @classmethod
    def from_po(cls, source):
        """Parse UTF-8 PO text atomically or raise LocalizationError."""
        encoded = _encode(source, 'PO source')
        handle, error = _void(), _void()
        _check(_lib.iil_catalog_create(encoded, len(encoded), ctypes.byref(handle), ctypes.byref(error)), error)
        return cls(handle)

    def _require_open(self):
        """Reject calls made after the catalog has been closed."""
        if not self._handle.value:
            raise RuntimeError('Catalog is closed')
        return self._handle

    @property
    def language(self):
        """Return the PO Language header or an empty string if absent."""
        with self._lock:
            output, error = _void(), _void()
            _check(_lib.iil_catalog_language(self._require_open(), ctypes.byref(output), ctypes.byref(error)), error)
            return _take(output)

    def translate_info(self, source, arguments=(), context=None):
        """Return translated text and the source of the selected result."""
        source_bytes = _encode(source, 'source')
        context_bytes = _encode(context, 'context') if context is not None else b''
        if isinstance(arguments, (str, bytes)):
            raise TypeError('arguments must be a sequence of strings')
        encoded = [_encode(value, 'argument') for value in arguments]
        pointers = (ctypes.c_char_p * len(encoded))(*encoded)
        lengths = (ctypes.c_size_t * len(encoded))(*(len(value) for value in encoded))
        output, error, origin = _void(), _void(), ctypes.c_int()
        with self._lock:
            code = _lib.iil_catalog_translate(
                self._require_open(), source_bytes, len(source_bytes), context_bytes, len(context_bytes),
                context is not None, pointers, lengths, len(encoded), ctypes.byref(output),
                ctypes.byref(origin), ctypes.byref(error))
            _check(code, error)
            text = _take(output)
        return Translation(text, ('translated', 'source_missing', 'source_invalid_translation')[origin.value])

    def translate(self, source, arguments=(), context=None):
        """Return translated and formatted text; use translate_info for provenance."""
        return self.translate_info(source, arguments, context).text

    def close(self):
        """Release the native handle exactly once after any active call completes."""
        with self._lock:
            if self._handle.value:
                _lib.iil_catalog_destroy(self._handle)
                self._handle = _void()

    def __enter__(self):
        """Keep this catalog open for a with-statement."""
        with self._lock:
            self._require_open()
        return self

    def __exit__(self, *_):
        """Release this catalog at the end of a with-statement."""
        self.close()

    def __del__(self):
        """Best-effort release for callers who did not explicitly close the catalog."""
        if getattr(self, '_handle', None) and self._handle.value:
            self.close()
