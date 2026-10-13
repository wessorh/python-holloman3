"""holloman3 — self-contained Python binding to the holloman3 fingerprint.

The holloman3 perceptual fingerprint maps an input buffer through a Hilbert
curve, downsamples it with a Lanczos-3 kernel, rotates the result 90°, and
returns 16 bytes, formatted as ``<order>.<32hex>``.

Usage::

    import holloman3
    holloman3.init(13)                       # allocate the curve cache
    print(holloman3.fingerprint(data_bytes)) # "h.8181…"
    holloman3.cleanup()

``init()`` must be called before ``fingerprint()``. Importing this package
fails (exits) if no supported SIMD (AVX2/AVX1/NEON) is available — there is no
scalar fallback.
"""

from . import _lib

__version__ = "0.1.0"


def init(order=13):
    """Initialize the Hilbert-curve cache.

    ``order`` is the maximum curve order; it must be >= the order required by
    the largest buffer you fingerprint (order 13 covers messages up to ~64 MB).
    """
    ret = _lib.init(order)
    if ret != 0:
        raise RuntimeError("holloman_init(%d) failed: error code %d" % (order, ret))


def cleanup():
    """Free the Hilbert-curve cache."""
    _lib.cleanup()


def is_avx2_available():
    """Return True if a supported SIMD path is present (AVX2/AVX1 on x86-64,
    NEON on ARM). The name is historical — it reports any supported SIMD."""
    return _lib.is_avx2_available()


def version():
    """Return the bundled native library version string."""
    return _lib.version()


def fingerprint(data):
    """Return the cluster id ``<order_char>.<32-hex-fingerprint>`` for ``data``.

    ``data`` must be a non-empty ``bytes``-like object and ``init()`` must have
    been called. The order character is derived automatically from the input
    size.
    """
    n = len(data)
    if n == 0:
        raise ValueError("empty buffer")

    order = _lib.order_for_size(n)
    raw = _lib.fingerprint_raw(data)
    ch = _lib.ORDER_ALPHABET[order] if order < len(_lib.ORDER_ALPHABET) else "?"
    return "%s.%s" % (ch, raw.hex())
