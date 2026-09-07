# holloman3-python

A self-contained Python 3 binding to the **holloman3** perceptual fingerprint:
Hilbert-curve mapping → Lanczos-3 downsampling → 90° rotation → a 16-byte
digest rendered as `<order>.<32hex>` (e.g. `h.8181858176797a780707070800000000`).

The package bundles the holloman3 **AVX2 assembly and C sources** and compiles
them into a shared library at install time. It has **no scalar fallback** — if
AVX2 is not present, it exits with an error message.

---

## Requirements

- **CPU with AVX2** (Intel Haswell 2013+, AMD Zen 2017+). Check on Linux:
  `grep -q avx2 /proc/cpuinfo`
- A C compiler (`gcc`) and GNU assembler (`as`).
- Python 3.8+.

## Install

```bash
pip install .
```

This compiles the bundled C + assembly into `holloman3/libholloman3.so` and
installs the `holloman3` package. The build is Linux-only for now (see
[Windows](#windows) below).

To run without installing, build the native library and put the project root
on `PYTHONPATH`:

```bash
python setup.py build_py            # builds holloman3/libholloman3.so
PYTHONPATH=. python -c "import holloman3; print(holloman3.version())"
```

---

## Quick start

```python
import holloman3

holloman3.init(13)                          # allocate the Hilbert-curve cache

data = open("sample.bin", "rb").read()
print(holloman3.fingerprint(data))          # "h.8181858176797a780707070800000000"

holloman3.cleanup()
```

If the CPU lacks AVX2, importing the package prints an error to stderr and
exits:

```
holloman3: AVX2 is not supported on this CPU; refusing to run (no scalar fallback).
```

---

## API

| Function | Description |
|---|---|
| `holloman3.init(order=13)` | Allocate the Hilbert-curve cache. **Must be called before `fingerprint()`.** `order` must be ≥ the order required by your largest input. |
| `holloman3.cleanup()` | Free the Hilbert-curve cache. |
| `holloman3.fingerprint(data)` | Return the cluster id `<order_char>.<32-hex>` for `data` (a `bytes`-like object). |
| `holloman3.is_avx2_available()` | Return `True`/`False`. |
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

The bundled assembly is written for the Linux **System V AMD64 ABI** and the
AVX2 CPUID check uses GCC inline assembly, so it does not build on Windows x64
as-is. A Windows port requires:

1. Rewriting the 4 `.s` files for the Microsoft x64 ABI (RCX/RDX/R8/R9, shadow
   space, `.seh_*` unwind info).
2. A portable CPUID (`__cpuid`/`_xgetbv`) in `arch_detect.c`, including the
   OSXSAVE/XCR0 check.
3. `_aligned_malloc`/`_aligned_free` in place of `posix_memalign`.
4. `__declspec(dllexport)` on the exported `holloman_*` functions.
5. A MinGW-w64 gcc / clang / MSVC toolchain and `libholloman3.dll` output.

The build step is isolated in `setup.py:build_native()`, so this drops in
without changing the Python API.
