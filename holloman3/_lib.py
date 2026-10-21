"""ctypes binding to the libholloman3 shared library.

The two hot, byte-identical Lanczos-3 scaling passes (``vscale``/``hscale``)
are implemented in libholloman3 (https://github.com/wessorh/libholloman3). This
module loads that shared library and declares the C signatures. The rest of the
fingerprint — Hilbert-curve mapping, kernel generation, rotation — is pure
Python (see ``_fingerprint.py``).
"""

import ctypes
import os

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
    # Fall back to the dynamic-loader search path (e.g. system install).
    return _LIB_NAMES[0]


_lib = ctypes.CDLL(_library_path())
_lib.holloman3_has_avx2.restype = ctypes.c_int
_lib.holloman3_vscale.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
_lib.holloman3_vscale.restype = None
_lib.holloman3_hscale.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
_lib.holloman3_hscale.restype = None


def is_avx2_available():
    """Return True if the CPU supports AVX2 (with OSXSAVE enabled)."""
    return _lib.holloman3_has_avx2() != 0


def version():
    """Return the library version string."""
    return "0.2.0"


def init(order):
    """No-op retained for API compatibility (curve/kernel generated on demand)."""
    return 0


def cleanup():
    """No-op retained for API compatibility."""
    return 0


def order_for_size(size):
    """Return the Hilbert-curve order for a buffer size (ceil(log2(size)/2))."""
    if size <= 0:
        return 0
    return ((size - 1).bit_length() + 1) // 2


def vscale(dst, src, coeffs, size):
    """Vertical Lanczos-3 scaling pass (dst/size*4, src/size*size, coeffs/4*size)."""
    _lib.holloman3_vscale(
        (ctypes.c_uint8 * len(dst)).from_buffer(dst),
        (ctypes.c_uint8 * len(src)).from_buffer(src),
        (ctypes.c_int32 * len(coeffs)).from_buffer(coeffs),
        size,
    )


def hscale(dst, src, coeffs, size):
    """Horizontal Lanczos-3 scaling pass (dst/16, src/size*4, coeffs/4*size)."""
    _lib.holloman3_hscale(
        (ctypes.c_uint8 * len(dst)).from_buffer(dst),
        (ctypes.c_uint8 * len(src)).from_buffer(src),
        (ctypes.c_int32 * len(coeffs)).from_buffer(coeffs),
        size,
    )
