# holloman3-python

A self-contained Python 3 binding to the **holloman3** perceptual fingerprint:
Hilbert-curve mapping → Lanczos-3 downsampling → 90° rotation → a 16-byte
digest rendered as `<order>.<32hex>` (e.g. `h.8181858176797a780707070800000000`).

The fingerprint's two SIMD scaling passes (Lanczos-3 vscale/hscale) are
delegated to **libholloman3** (https://github.com/wessorh/libholloman3), vendored
as a git submodule under `third_party/libholloman3`; everything else (Hilbert
mapping, kernel generation, rotation) is pure Python. libholloman3 provides
four byte-identical backends:

- **x86-64, AVX2** (256-bit) — Intel Haswell 2013+, AMD Zen 2017+.
- **x86-64, AVX1 / SSE4.1** (128-bit) — Intel Sandy/Ivy Bridge 2011+, AMD Bulldozer.
- **ARM, NEON** — aarch64.
- **scalar** — portable fallback (always available).

---

## Requirements

- Python 3.8+.
- A C compiler + `make` (gcc/clang on Linux/macOS; MinGW-w64 on Windows; a
  native/cross ARM toolchain on aarch64).
- The libholloman3 submodule checked out (`git submodule update --init`).

## Install

```bash
git submodule update --init --recursive
pip install .
```

`setup.py` builds `libholloman3.so` by invoking the submodule's Makefile (which
auto-detects AVX2/SSE4.1 vs NEON) and copies it into the package for ctypes to
load.

To run without installing:

```bash
make -C third_party/libholloman3 libholloman3.so
cp third_party/libholloman3/libholloman3.so holloman3/
PYTHONPATH=. python -c "import holloman3; print(holloman3.version())"
```

---

## Quick start

```python
import holloman3

data = open("sample.bin", "rb").read()
print(holloman3.fingerprint(data))          # "h.8181858176797a780707070800000000"
```

`init()` / `cleanup()` are retained for API compatibility but are no-ops — the
Hilbert curve and Lanczos kernel are generated on demand and cached.

---

## API

| Function | Description |
|---|---|
| `holloman3.init(order=13)` | Allocate the Hilbert-curve cache. **Must be called before `fingerprint()`.** `order` must be ≥ the order required by your largest input. |
| `holloman3.cleanup()` | Free the Hilbert-curve cache. |
| `holloman3.fingerprint(data)` | Return the cluster id `<order_char>.<32-hex>` for `data` (a `bytes`-like object). |
| `holloman3.is_avx2_available()` | Return `True` if a supported SIMD path is present (AVX2/AVX1 on x86-64, NEON on ARM). The name is historical — it reports *any* supported SIMD, not just AVX2. |
| `holloman3.version()` | Return the bundled native library version string. |

`fingerprint()` derives the leading order character automatically from the
input size: `order = ceil(log2(size)/2)`, mapped through the alphabet
`"  cdefghijkmnopqrstuvwxyz"` — order 7 → `h` (~16 KB), 10 → `k` (~1 MB),
13 → `o` (~64 MB).

### Fingerprinting multiple buffers

Reuse one `init()` for many fingerprints:

```python
import holloman3

holloman3.init(13)
try:
    for path in ("a.bin", "b.bin"):
        with open(path, "rb") as f:
            print(path, holloman3.fingerprint(f.read()))
finally:
    holloman3.cleanup()
```

> **Memory note:** `init(13)` allocates ~640 MB of curve cache (order-12 even +
> order-13 odd curves). It supports inputs up to ~64 MB. Pass a lower order for
> a smaller footprint, e.g. `init(10)` (~40 MB, up to ~1 MB inputs).

---

## Example — SMTP fingerprints

`examples/smtp_fingerprint.py` implements the smtp-feed pipeline for a single
`.eml` file, printing three holloman3 fingerprints to stdout:

| Label | Meaning |
|---|---|
| `header_norm` | Normalized headers — per-message headers (Message-ID, Received, Date, Subject, DKIM-Signature, …) dropped, and From/Reply-To/Sender reduced to their `@domain`. |
| `body` | The message body. |
| `message` | The entire raw message. |

Create a sample message and run it:

```bash
cat > sample.eml <<'EOF'
From: "Alice Smith" <alice@example-phish.net>
Reply-To: alice@example-phish.net
To: victim@example.com
Subject: Urgent: verify your account
Date: Tue, 7 Sep 2026 10:00:00 -0400
Message-ID: <20260907140000.12345@example-phish.net>
Received: from mail.example-phish.net (mail.example-phish.net [203.0.113.5])
	by mx.example.com with ESMTPS id 9999
Content-Type: multipart/alternative; boundary="----=_Part_123"

This is a plain text body with a phishing link.

Click here: http://example-phish.net/login
EOF

python examples/smtp_fingerprint.py sample.eml
```

Output (order characters vary with the input size):

```
header_norm: e.64605b635105535e0200060600000000
body:        e.4550565a0006555e0000050500000000
message:     f.5659595e524341590700000500000000
```

`header_norm` clusters by sender infrastructure (a "campaign envelope"
signature); `body` and `message` identify the specific content.

---

## Windows

Windows x64 is supported. The package bundles both assembly variants — the
System V AMD64 `.s` files for Linux and Microsoft x64 ABI `*_win64.s` files for
Windows — and `setup.py` selects the right set at build time. The C sources are
portable (intrinsics-based CPUID, `_aligned_malloc` on `_WIN32`,
`__declspec(dllexport)`).

```bash
# On Windows (MinGW-w64 gcc on PATH):
pip install .       # builds holloman3/libholloman3.dll
```

Requires an AVX2 or AVX1 CPU and a compiler that can assemble GAS `.s` files —
MinGW-w64 gcc (recommended) or clang targeting `x86_64-pc-windows-msvc`.

The Windows build has been cross-compiled with MinGW and verified under Wine:
the fingerprint output is byte-identical to Linux. It has not yet been run on a
native Windows host. See `docs/windows-port.md` for the cross-compile recipe and
the ABI port details.
