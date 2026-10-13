"""Build/install for holloman3.

The Python package is a thin ctypes wrapper around a shared library compiled
from the bundled holloman3 native sources. Three paths are selected at build
time by target architecture:

  * x86-64  — AVX2 assembly (256-bit) + a 128-bit SSE4.1 fallback for AVX1-only
              CPUs, runtime-dispatched via CPUID (see arch_detect.c).
  * ARM     — the self-contained NEON library (holloman_neon.c + asm).

The output is always ``libholloman3.so`` / ``libholloman3.dll`` exposing the
same ``holloman_*`` API, so the ctypes wrapper is unchanged across platforms.
"""

import os
import platform
import shutil
import subprocess
import sys
import tempfile

from setuptools import setup, find_packages
from setuptools.command.build_py import build_py as _build_py

HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE_DIR = os.path.join(HERE, "holloman3", "_native")

# --- x86 AVX2 path (mirrors holloman5 src/avx2/Makefile) ---------------------
AVX2_DIR = os.path.join(NATIVE_DIR, "avx2")
AVX2_C_SOURCES = [
    "hilbert-avx2/src/hilbert.c",
    "hilbert-avx2/src/hilbert_mapping.c",
    "hilbert-avx2/src/hilbert_utils.c",
    "hilbert-avx2/src/arch_detect.c",
    "hilbert-avx2/src/hilbert_external_buffer.c",
    "holloman_avx2.c",
    "lanczos_avx2_optimized.c",
]
AVX2_ASM = [
    "hilbert-avx2/src/hilbert_avx2.s",
    "hilbert-avx2/src/hilbert_mapping_avx2.s",
    "hilbert-avx2/src/hilbert_mapping_avx2_cache_blocking.s",
    "hilbert-avx2/src/hilbert_utils_avx2.s",
]
AVX2_ASM_WIN64 = [
    "hilbert-avx2/src/hilbert_avx2_win64.s",
    "hilbert-avx2/src/hilbert_mapping_avx2_win64.s",
    "hilbert-avx2/src/hilbert_mapping_avx2_cache_blocking_win64.s",
    "hilbert-avx2/src/hilbert_utils_avx2_win64.s",
]
# 128-bit SSE4.1 path (AVX1-only CPUs) — compiled with -msse4.1, not -mavx2.
SSE41_SOURCES = [
    "hilbert-avx2/src/hilbert_sse41.c",
    "lanczos_sse41.c",
]

# --- ARM NEON path (mirrors holloman5 src/neon/Makefile) ----------------------
NEON_DIR = os.path.join(NATIVE_DIR, "neon")
NEON_SOURCES = [
    "holloman_neon.c",
    "hilbert_external_buffer_neon.c",
    "hilbert_external_buffer_neon.s",
]


def _compile(cc, cflags, src, obj, cwd):
    subprocess.check_call([cc] + cflags + ["-c", src, "-o", obj], cwd=cwd)


def build_native():
    """Compile the bundled C + assembly into a shared library in the package."""
    is_windows = sys.platform == "win32"
    machine = platform.machine().lower()
    libname = "libholloman3.dll" if is_windows else "libholloman3.so"
    cc = os.environ.get("CC", "gcc")

    pic = ["-fPIC"] if not is_windows else ["-DHOLLOMAN_BUILD_DLL"]
    is_arm = machine.startswith(("aarch64", "armv7", "arm", "arm64"))

    if is_arm:
        # ARM: build the self-contained NEON library.
        if not os.path.isdir(NEON_DIR):
            raise SystemExit("holloman3: missing NEON sources at %s" % NEON_DIR)
        src_dir = NEON_DIR
        sources = NEON_SOURCES
        if machine.startswith("aarch64") or machine.startswith("arm64"):
            archflags = ["-march=armv8-a+simd"]
        else:
            archflags = ["-mfpu=neon", "-mfloat-abi=hard"]
    else:
        # x86: AVX2 asm + 128-bit SSE4.1 path, runtime-dispatched.
        if not os.path.isdir(AVX2_DIR):
            raise SystemExit("holloman3: missing AVX2 sources at %s" % AVX2_DIR)
        src_dir = AVX2_DIR
        asm = AVX2_ASM_WIN64 if is_windows else AVX2_ASM
        sources = AVX2_C_SOURCES + asm + SSE41_SOURCES
        archflags = []

    tmp = tempfile.mkdtemp(prefix="holloman3-build-")
    try:
        objs = []
        for i, src in enumerate(sources):
            if is_arm:
                cflags = ["-O3", "-I../common"] + archflags + pic
            elif src in SSE41_SOURCES:
                # 128-bit integer SIMD only — safe on AVX1-only CPUs.
                cflags = ["-O3", "-msse4.1", "-I../common", "-Ihilbert-avx2/include"] + pic
            else:
                cflags = ["-O3", "-mavx2", "-I../common", "-Ihilbert-avx2/include"] + pic
            obj = os.path.join(tmp, "obj_%02d.o" % i)
            _compile(cc, cflags, src, obj, src_dir)
            objs.append(obj)

        out = os.path.join(tmp, libname)
        link = [cc, "-shared", "-o", out] + objs
        if not is_windows:
            link.append("-lm")
        subprocess.check_call(link, cwd=src_dir)

        out_path = os.path.join(HERE, "holloman3", libname)
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        os.replace(out, out_path)
        print("holloman3: built %s (%s)" % (out_path, machine))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


class build_py(_build_py):
    def run(self):
        build_native()
        super().run()


setup(
    name="holloman3",
    version="0.1.0",
    description="Self-contained Python binding to the holloman3 perceptual fingerprint (Hilbert curve + Lanczos-3)",
    long_description=open(os.path.join(HERE, "README.md"), encoding="utf-8").read(),
    long_description_content_type="text/markdown",
    packages=find_packages(exclude=("tests", "examples")),
    package_data={"holloman3": ["libholloman3.so", "libholloman3.dll"]},
    python_requires=">=3.8",
    cmdclass={"build_py": build_py},
)
