"""holloman3 — Python binding to the holloman3 perceptual fingerprint.

The holloman3 fingerprint maps an input buffer through a Hilbert curve,
downsamples it with a Lanczos-3 kernel, rotates the result 90°, and returns 16
bytes, formatted as ``<order>.<32hex>``.

The two SIMD scaling passes are delegated to libholloman3
(https://github.com/wessorh/libholloman3); the rest (Hilbert mapping, kernel
generation, rotation) is pure Python.

Usage::

    import holloman3
    print(holloman3.fingerprint(data_bytes))  # "h.8181…"
"""

from . import _fingerprint, _lib

__version__ = "0.2.0"


def init(order=13):
    """Initialize the fingerprint (no-op: the curve/kernel are generated on
    demand). Retained for API compatibility."""
    _lib.init(order)


def cleanup():
    """Free cached state (no-op). Retained for API compatibility."""
    _lib.cleanup()


def is_avx2_available():
    """Return True if the CPU supports AVX2 (with OSXSAVE enabled)."""
    return _lib.is_avx2_available()


def version():
    """Return the library version string."""
    return _lib.version()


def fingerprint(data):
    """Return the cluster id ``<order_char>.<32-hex-fingerprint>`` for ``data``.

    ``data`` must be a non-empty ``bytes``-like object. The order character is
    derived automatically from the input size.
    """
    n = len(data)
    if n == 0:
        raise ValueError("empty buffer")

    order = _lib.order_for_size(n)
    raw = _fingerprint.fingerprint_raw(data)
    ch = _lib.ORDER_ALPHABET[order] if order < len(_lib.ORDER_ALPHABET) else "?"
    return "%s.%s" % (ch, raw.hex())
