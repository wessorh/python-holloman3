"""Pure-Python holloman3 fingerprint.

Hilbert-curve mapping, Lanczos-3 kernel generation, and rotation are implemented
here in pure Python; the two SIMD scaling passes (vscale/hscale) are delegated
to libholloman3 via ``_lib``. Output is byte-identical to the C and Go
implementations.
"""

import math
from array import array

from . import _lib

_BITS = 14
_SCALE_FACTOR = 1 << _BITS
_ROUND_OFFSET = 1 << (_BITS - 1)
_ALPHA = 3.0

_kernel_cache = {}


def _calculate_order(size):
    if size == 0:
        return 0
    return ((size - 1).bit_length() + 1) >> 1


def _hilbert_d2xy(d, order):
    x = y = 0
    s = 1
    for _ in range(order):
        rx = (d >> 1) & 1
        ry = (d ^ rx) & 1
        if ry == 0:
            if rx == 1:
                x = s - 1 - x
                y = s - 1 - y
            x, y = y, x
        x += s * rx
        y += s * ry
        d >>= 2
        s <<= 1
    return (x << 32) | y


def _map_buffer(data, order):
    dim = 1 << order
    mapped = bytearray(dim * dim)
    for i, b in enumerate(data):
        p = _hilbert_d2xy(i, order)
        x = p >> 32
        y = p & 0xFFFFFFFF
        mapped[y * dim + x] = b
    return mapped


def _lanczos3(x):
    if x > _ALPHA:
        return 0.0
    if x == 0.0:
        return 1.0
    b = x * math.pi
    c = b / _ALPHA
    return math.sin(b) * math.sin(c) / (b * c)


def _make_double_kernel(input_size, output_size):
    scale = output_size / input_size
    step = scale if scale < 1.0 else 1.0
    taps = int(math.ceil(3.0 / step)) * 2
    if taps > input_size:
        taps = input_size & ~1

    xstep = 1.0 / scale
    xmid = (input_size - output_size) / (output_size * 2)

    weights = [0.0] * (output_size * taps)
    sums = [0.0] * output_size
    offsets = [0] * output_size

    for i in range(output_size):
        left = int(math.ceil(xmid)) - taps // 2
        hi = input_size - taps
        if hi < 0:
            hi = 0
        x = left
        if x < 0:
            x = 0
        elif x > hi:
            x = hi
        offsets[i] = x

        s = 0.0
        for j in range(taps):
            src = left + j
            weight = _lanczos3(abs(xmid - src) * step)
            sc = src
            if sc < x:
                sc = x
            elif sc > input_size - 1:
                sc = input_size - 1
            sc -= x
            weights[i * taps + sc] += weight
            s += weight
        sums[i] = s
        xmid += xstep

    return weights, sums, offsets, taps


def _make_integer_kernel(taps, size, weights, sums):
    coeffs = [0] * (size * taps)
    for i in range(size):
        base = i * taps
        # Stable sort by |weight| descending (matches glibc qsort).
        order = sorted(range(taps), key=lambda j: -abs(weights[base + j]))
        diff = 0.0
        scale = _SCALE_FACTOR / sums[i]
        for j in order:
            wv = weights[base + j] * scale + diff
            iw = math.floor(wv + 0.5)
            coeffs[base + j] = int(iw)
            diff = wv - iw
    return coeffs


def _get_kernel(dim):
    k = _kernel_cache.get(dim)
    if k is not None:
        return k
    vw, vs, _, vt = _make_double_kernel(dim, 4)
    vc = _make_integer_kernel(vt, 4, vw, vs)
    hw, hs, _, ht = _make_double_kernel(dim, 4)
    hc = _make_integer_kernel(ht, 4, hw, hs)
    k = (array("i", vc), array("i", hc))
    _kernel_cache[dim] = k
    return k


def _rotate90(src):
    mapping = (12, 8, 4, 0, 13, 9, 5, 1, 14, 10, 6, 2, 15, 11, 7, 3)
    return bytes(src[m] for m in mapping)


def fingerprint_raw(data):
    """Return the raw 16-byte fingerprint for ``data`` as ``bytes``."""
    n = len(data)
    if n == 0:
        raise ValueError("empty buffer")

    order = _calculate_order(n)
    dim = 1 << order
    mapped = _map_buffer(data, order)

    v_coeffs, h_coeffs = _get_kernel(dim)

    intermediate = bytearray(dim * 4)
    _lib.vscale(intermediate, mapped, v_coeffs, dim)

    tmp = bytearray(16)
    _lib.hscale(tmp, intermediate, h_coeffs, dim)

    return _rotate90(tmp)
