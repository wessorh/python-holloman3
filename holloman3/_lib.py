"""ctypes binding to the bundled holloman3 native shared library.

The library is compiled from the native assembly + C sources bundled under
`holloman3/_native/` at install time (see ``setup.py``). This module loads it,
declares the C function signatures, and enforces the "no fallback" policy: if
no supported SIMD (AVX2/AVX1 on x86-64, NEON on ARM) is available on this CPU,
the process exits with an error.
"""

import ctypes
import os
import sys

# Maps a Hilbert-curve order index to the human-readable cluster-id letter.
# Order 7 -> 'h', 10 -> 'k', 13 -> 'o'.
ORDER_ALPHABET = "  cdefghijkmnopqrstuvwxyz"

_LIB_NAMES = ("libholloman3.so", "libholloman3.dylib", "libholloman3.dll")


def _library_path():
    here = os.path.dirname(os.path.abspath(__file__))
    for name in _LIB_NAMES:
        path = os.path.join(here, name)
        if os.path.exists(path):
            return path
    raise FileNotFoundError(
        "holloman3 native library not found. Build it with `pip install .` "
        "(or `python setup.py build_py`)."
    )


_lib = ctypes.CDLL(_library_path())

_lib.holloman_init.argtypes = [ctypes.c_uint32]
_lib.holloman_init.restype = ctypes.c_int
_lib.holloman_cleanup.restype = ctypes.c_int
_lib.holloman_fingerprint_buffer.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
_lib.holloman_fingerprint_buffer.restype = ctypes.c_int
_lib.holloman_is_avx2_available.restype = ctypes.c_int
_lib.holloman_version.restype = ctypes.c_char_p


def is_avx2_available():
    """Return True if a supported SIMD path is present (AVX2/AVX1 on x86-64,
    NEON on ARM). The name is historical — it reports any supported SIMD."""
    return _lib.holloman_is_avx2_available() != 0


def version():
    """Return the bundled native library version string."""
    v = _lib.holloman_version()
    return v.decode("utf-8", "replace") if v else ""


def init(order):
    """Initialize the Hilbert-curve cache. Returns the raw C return code."""
    return _lib.holloman_init(ctypes.c_uint32(order))


def cleanup():
    """Free the Hilbert-curve cache. Returns the raw C return code."""
    return _lib.holloman_cleanup()


def order_for_size(size):
    """Return the Hilbert-curve order for a buffer size (mirrors the C
    ``hilbert_calculate_order``: ``ceil(log2(size)/2)``)."""
    if size <= 0:
        return 0
    return ((size - 1).bit_length() + 1) // 2


def fingerprint_raw(data):
    """Return the raw 16-byte fingerprint for ``data`` as ``bytes``.

    ``init()`` must have been called first.
    """
    n = len(data)
    if n == 0:
        raise ValueError("empty buffer")

    buf = ctypes.create_string_buffer(data, n)
    out = (ctypes.c_ubyte * 16)()
    ret = _lib.holloman_fingerprint_buffer(buf, ctypes.c_size_t(n), out)
    if ret != 0:
        raise RuntimeError("holloman_fingerprint_buffer failed: error code %d" % ret)
    return bytes(out)


# "No fallback" gate: exit with a clear message if no supported SIMD is present.
if not is_avx2_available():
    sys.stderr.write(
        "holloman3: no supported SIMD (AVX2/AVX1/NEON) on this CPU; "
        "refusing to run (no scalar fallback).\n"
    )
    sys.exit(1)
