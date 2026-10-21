"""Build/install for holloman3.

The Python package is a thin ctypes wrapper around the libholloman3 shared
library (https://github.com/wessorh/libholloman3), vendored as a git submodule
under ``third_party/libholloman3``. Only the two SIMD scaling kernels live in C;
the rest of the fingerprint (Hilbert mapping, kernel generation, rotation) is
pure Python.

``build_py`` builds ``libholloman3.so`` by invoking the submodule's Makefile
(which auto-detects x86-64 AVX2/SSE4.1 vs aarch64 NEON) and copies it into the
package directory for ctypes to load.
"""

import os
import shutil
import subprocess

from setuptools import setup, find_packages
from setuptools.command.build_py import build_py as _build_py

HERE = os.path.dirname(os.path.abspath(__file__))
LIB_DIR = os.path.join(HERE, "third_party", "libholloman3")


def build_native():
    """Build the libholloman3 shared library and place it in the package."""
    if not os.path.isfile(os.path.join(LIB_DIR, "src", "scale.c")):
        raise SystemExit(
            "libholloman3 submodule not checked out. Run:\n"
            "  git submodule update --init --recursive"
        )

    subprocess.check_call(["make", "libholloman3.so"], cwd=LIB_DIR)

    src = os.path.join(LIB_DIR, "libholloman3.so")
    dst = os.path.join(HERE, "holloman3", "libholloman3.so")
    shutil.copy(src, dst)
    print("holloman3: built %s" % dst)


class build_py(_build_py):
    def run(self):
        build_native()
        super().run()


setup(
    name="holloman3",
    version="0.2.0",
    description="Python binding to the holloman3 perceptual fingerprint (libholloman3 SIMD kernels)",
    long_description=open(os.path.join(HERE, "README.md"), encoding="utf-8").read(),
    long_description_content_type="text/markdown",
    packages=find_packages(exclude=("tests", "examples")),
    package_data={"holloman3": ["libholloman3.so", "libholloman3.dll"]},
    python_requires=">=3.8",
    cmdclass={"build_py": build_py},
)
