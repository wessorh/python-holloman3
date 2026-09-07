"""Build/install for holloman3.

The Python package is a thin ctypes wrapper around a shared library compiled
from the bundled holloman3 AVX2 C + assembly sources. This build step is
isolated here so the Windows port (Microsoft x64 ABI) can later drop in without
touching the Python API — see README.md.
"""

import os
import shutil
import subprocess
import sys
import tempfile

from setuptools import setup, find_packages
from setuptools.command.build_py import build_py as _build_py

HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE_DIR = os.path.join(HERE, "holloman3", "_native", "avx2")
OUT_LIB = os.path.join(HERE, "holloman3", "libholloman3.so")

# Bundled sources, relative to NATIVE_DIR (mirrors holloman5 src/avx2/Makefile).
C_SOURCES = [
    "hilbert-avx2/src/hilbert.c",
    "hilbert-avx2/src/hilbert_mapping.c",
    "hilbert-avx2/src/hilbert_utils.c",
    "hilbert-avx2/src/arch_detect.c",
    "hilbert-avx2/src/hilbert_external_buffer.c",
    "holloman_avx2.c",
    "lanczos_avx2_optimized.c",
]
ASM_SOURCES = [
    "hilbert-avx2/src/hilbert_avx2.s",
    "hilbert-avx2/src/hilbert_mapping_avx2.s",
    "hilbert-avx2/src/hilbert_mapping_avx2_cache_blocking.s",
    "hilbert-avx2/src/hilbert_utils_avx2.s",
]


def build_native():
    """Compile the bundled C + assembly into a shared library in the package.

    Linux only for now. `-fPIC` is required for a shared object; `-mavx2` is
    used (rather than `-march=znver2`) so the build runs on any AVX2 CPU.
    """
    if sys.platform != "linux":
        raise SystemExit(
            "holloman3: the native library currently builds on Linux only.\n"
            "Windows support (Microsoft x64 ABI port) is a planned follow-up — "
            "see README.md."
        )
    if not os.path.isdir(NATIVE_DIR):
        raise SystemExit("holloman3: missing bundled native sources at %s" % NATIVE_DIR)

    cc = os.environ.get("CC", "gcc")
    cflags = ["-O3", "-mavx2", "-fPIC", "-I../common", "-Ihilbert-avx2/include"]

    tmp = tempfile.mkdtemp(prefix="holloman3-build-")
    try:
        objs = []
        for i, src in enumerate(C_SOURCES + ASM_SOURCES):
            obj = os.path.join(tmp, "obj_%02d.o" % i)
            subprocess.check_call([cc] + cflags + ["-c", src, "-o", obj], cwd=NATIVE_DIR)
            objs.append(obj)

        out = os.path.join(tmp, "libholloman3.so")
        subprocess.check_call([cc, "-shared", "-o", out] + objs + ["-lm"], cwd=NATIVE_DIR)

        os.makedirs(os.path.dirname(OUT_LIB), exist_ok=True)
        os.replace(out, OUT_LIB)
        print("holloman3: built %s" % OUT_LIB)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


class build_py(_build_py):
    def run(self):
        build_native()
        super().run()


setup(
    name="holloman3",
    version="0.1.0",
    description="Self-contained Python binding to the holloman3 AVX2 perceptual fingerprint (Hilbert curve + Lanczos-3)",
    long_description=open(os.path.join(HERE, "README.md"), encoding="utf-8").read(),
    long_description_content_type="text/markdown",
    packages=find_packages(exclude=("tests", "examples")),
    package_data={"holloman3": ["libholloman3.so"]},
    python_requires=">=3.8",
    cmdclass={"build_py": build_py},
)
