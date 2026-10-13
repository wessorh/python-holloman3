# Windows x64 port

> **Status: implemented and verified via cross-compile + Wine.** The MS x64 ABI
> assembly, portable CPUID, aligned-allocation, and DLL-export changes below are
> all in place. The DLL cross-compiles with MinGW and its fingerprint output is
> byte-identical to Linux when run under Wine. It has not yet been run on a
> native Windows host.

The original Linux-only situation: the bundled native sources were written for
the Linux **System V AMD64 ABI** and used GCC inline assembly for the CPUID
check.

## What needs to change (and what does not)

The native build has three layers:

| Layer | Files | Windows impact |
|---|---|---|
| Raw AVX2 assembly | `hilbert_avx2.s`, `hilbert_mapping_avx2.s`, `hilbert_mapping_avx2_cache_blocking.s`, `hilbert_utils_avx2.s` | **Must rewrite for the MS x64 ABI** |
| AVX2 intrinsics | `lanczos_avx2_optimized.c` | **None** — `<immintrin.h>` `_mm256_*` intrinsics are portable across gcc/clang/MSVC |
| Portable C | `holloman_avx2.c`, `hilbert.c`, `hilbert_mapping.c`, `hilbert_utils.c`, `hilbert_external_buffer.c` | Small changes: CPUID (`arch_detect.c`), aligned allocation, DLL export |

## 1. Assembly ABI rewrite (the core work)

The four `.s` files use System V register conventions. On Windows x64
(Microsoft ABI) the integer-argument registers and non-volatile set differ:

| | System V (Linux) | Microsoft x64 (Windows) |
|---|---|---|
| Arg 1..4 | RDI, RSI, RDX, RCX | RCX, RDX, R8, R9 |
| Arg 5+ | R8, R9, stack | **stack only** |
| Volatile (caller-saved) | rax, rcx, rdx, rsi, rdi, r8–r11 | rax, rcx, rdx, r8–r11 |
| Non-volatile (callee-saved) | rbx, rbp, r12–r15 | **rbx, rbp, rdi, rsi, rsp, r12–r15**, xmm6–xmm15 |
| Stack at entry | 8-byte return addr | return addr **+ 32-byte shadow space** |

Two subtle consequences drive most of the edits:

1. **rdi/rsi are callee-saved on Windows** but several functions use them as
   scratch — they must be saved/restored (or swapped to a volatile register).
2. **The 5th argument moves to the stack** (at `[rsp+40]` on entry, before the
   shadow space), which affects the two 5-argument mapping functions.

Per-function changes (verified against the current sources):

### `hilbert_d2xy_batch_avx2(distances, packed, count, order)` — 4 args
- Read args from **RCX, RDX, R8, R9** and copy to r10/r11/r12/r9d as today.
- The scalar path uses **EDI/ESI/RDI/RSI as scratch** → add `pushq %rdi`/`%rsi`
  to the prologue (and matching pops), since they are non-volatile on Windows.

### `hilbert_map_buffer_avx2_internal(src, dst, curve, count, dimension)` — 5 args
- Read args from **RCX, RDX, R8, R9** and read `dimension` from **`40(%rsp)`**
  *before* the pushes.
- No rdi/rsi scratch usage → no extra registers to save.

### `hilbert_map_buffer_avx2_cache_blocking(src, dst, curve, count, dimension)` — 5 args
- Same arg layout as above (dimension from stack).
- The inner loop uses **RSI as scratch** → add `pushq %rsi`.

### `hilbert_zero_buffer_avx2(buffer, length)` — 2 args
- Read args from **RCX, RDX**; copy `rcx→rdi`, `rdx→rsi` and `pushq %rdi`/`%rsi`
  (the body addresses buffer via rdi and length via rsi throughout).

### Assembly housekeeping
- Drop the ELF-only `.note.GNU-stack` directive (harmless on COFF, but remove).
- Add `.seh_proc`/`.seh_endprologue`/`.seh_endproc` unwind directives if the
  DLL must support stack unwinding. Not strictly required — none of these
  functions call anything or raise — but recommended for a production DLL.
- Maintain 16-byte stack alignment after the prologue.

## 2. Portable CPUID — `arch_detect.c`

The current `hilbert_has_simd()` uses GCC inline `__asm__` (`pushfq` + `cpuid`),
which MSVC cannot compile (MSVC has no inline asm on x64). Replace it with
compiler intrinsics, guarded by compiler:

```c
#if defined(_MSC_VER)
  #include <intrin.h>
  // __cpuidex(regs, 7, 0); (regs[1] & (1<<5)) != 0  → AVX2
  // + OS support: (_xgetbv(0) & 0x6) == 0x6          → XSAVE/YMM state enabled
#else
  #include <cpuid.h>
  // __get_cpuid_count(7, 0, &a,&b,&c,&d); (b & (1<<5)) != 0
  // + xgetbv via __builtin_ia32_xgetbv(0)
#endif
```

Also add the **OSXSAVE + XCR0 check** the current code omits: on Windows, AVX2
requires OS support for YMM state (`XCR0[2:1] == 0b11`), not just the CPUID
leaf-7 EBX bit 5. Guard `hilbert_init_arch()` on `_MSC_VER` too (the `__x86_64__`
vs `_M_X64` split already exists).

## 3. Aligned allocation — `hilbert.c`, `hilbert_mapping.c`

`posix_memalign()` (POSIX) is used at `hilbert.c:94,120` and
`hilbert_mapping.c:223,306`. On Windows use `_aligned_malloc(size, 32)` /
`_aligned_free`. Introduce a small portable helper in a shared header (e.g.
`holloman_aligned_alloc`/`holloman_aligned_free`) and update the matching
`free()` calls — Windows `_aligned_malloc` must be released with
`_aligned_free`, **not** `free()`.

## 4. DLL export — `holloman.h`

Linux `.so` files export all non-static symbols; Windows DLLs export nothing by
default. Add an export macro to the public `holloman_*` functions:

```c
#if defined(_WIN32) && defined(HOLLOMAN_BUILD_DLL)
  #define HOLLOMAN_API __declspec(dllexport)
#elif defined(_WIN32)
  #define HOLLOMAN_API __declspec(dllimport)
#else
  #define HOLLOMAN_API
#endif
```

Annotate `holloman_init`, `holloman_cleanup`, `holloman_fingerprint_buffer`,
`holloman_fingerprint_file`, `holloman_fingerprint_buffer_external`,
`holloman_is_avx2_available`, `holloman_version`, and `hilbert_calculate_order`
(used for the order character). `_lib.py` already searches for
`libholloman3.dll`.

## 5. Build step — `setup.py`

`build_native()` currently bails on non-Linux. Extend it to detect Windows and
emit `libholloman3.dll`:

- Detect compiler: `x86_64-w64-mingw32-gcc` / `gcc` (MinGW) or `clang-cl`.
- Flags: `-O3 -mavx2` (MinGW/clang; MSVC would use `/O2 /arch:AVX2`), `-shared`
  (MinGW `-shared` produces a DLL), define `HOLLOMAN_BUILD_DLL`, drop `-lm`.
- Output `holloman3/libholloman3.dll`.

## 6. Toolchain requirements

To build on Windows x64, install **one** of these toolchains (recommended
first):

| Toolchain | Can assemble the `.s` files? | Install |
|---|---|---|
| **MinGW-w64 GCC** *(recommended)* | ✅ GNU `as` handles both AT&T and `.intel_syntax` | MSYS2: `pacman -S mingw-w64-x86_64-gcc` · Chocolatey: `choco install mingw` · or winlibs.com prebuilt |
| **clang** (`--target=x86_64-pc-windows-msvc`) | ✅ integrated assembler parses GAS syntax | `winget install LLVM.LLVM` |
| **MSVC Build Tools** | ❌ MASM ≠ GAS syntax — would require rewriting all four `.s` files | `winget install Microsoft.VisualStudio.2022.BuildTools` |

Plus:

- **Python 3.8+ for Windows** (`winget install Python.Python.3.12` or python.org).
- **An AVX2-capable CPU** (all modern x86-64; verify on Windows with
  `coreinfo`/CPU-Z, or `python -c "import holloman3"` which will exit with the
  no-fallback message if absent).

## 7. Verification plan

1. On a Windows x64 box with the toolchain on `PATH`: `pip install .` — the
   `build_py` step compiles the MS-ABI assembly and produces `libholloman3.dll`.
2. `python -c "import holloman3; holloman3.init(13); print(holloman3.fingerprint(b'A'*4096))"`
   → must print `g.41414141414141414141414141414141` (identical to Linux).
3. Run the full test suite: `python -m unittest discover -s tests` — the
   `test_known_value` assertion (`h.8181858176797a780707070800000000`) must
   hold byte-for-byte, proving the ABI port is correct.
4. Run `python examples/smtp_fingerprint.py sample.eml` and compare to Linux
   output.
5. Negative test: on a non-AVX2 machine (or a VM without AVX2 passthrough),
   importing the package must print the AVX2 error and exit non-zero.

## 8. Cross-compiling from Linux (how this was verified)

With `gcc-mingw-w64-x86-64` installed, the DLL and a test harness build and run
under Wine:

```bash
cd holloman3/_native/avx2

# 1. Compile the portable C + the MS-ABI assembly into the DLL:
x86_64-w64-mingw32-gcc -O3 -mavx2 -DHOLLOMAN_BUILD_DLL \
  -I../common -Ihilbert-avx2/include \
  -shared \
  hilbert-avx2/src/hilbert.c \
  hilbert-avx2/src/hilbert_mapping.c \
  hilbert-avx2/src/hilbert_utils.c \
  hilbert-avx2/src/arch_detect.c \
  hilbert-avx2/src/hilbert_external_buffer.c \
  holloman_avx2.c lanczos_avx2_optimized.c \
  hilbert-avx2/src/hilbert_avx2_win64.s \
  hilbert-avx2/src/hilbert_mapping_avx2_win64.s \
  hilbert-avx2/src/hilbert_mapping_avx2_cache_blocking_win64.s \
  hilbert-avx2/src/hilbert_utils_avx2_win64.s \
  -o libholloman3.dll

# 2. Build a test harness and run it under Wine:
x86_64-w64-mingw32-gcc -O3 -mavx2 -DHOLLOMAN_BUILD_DLL \
  -I../common -Ihilbert-avx2/include \
  win_test.c <same sources as above> -o win_test.exe
wine ./win_test.exe   # prints fingerprints; compare against the Linux build
```

Result: `holloman_fingerprint_buffer` under Wine is byte-identical to Linux for
multiple input sizes, and `objdump -p libholloman3.dll` lists all seven
`holloman_*` symbols in the export table (the ctypes binding loads them by
name).

The Python API and the rest of the package are untouched — `_lib.py` already
loads `libholloman3.dll` by name and the AVX2 gate is compiler-independent.
