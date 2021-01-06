import os
import os.path
import shutil
import stat
import subprocess

from setuptools import Extension, setup
from Cython.Build import cythonize
from Cython.Compiler import Options

VERSION = "0.8.5"

# A helper for rmtree. On Windows, read-only files can't be deleted by rmtree, so we make them not readonly.
def handle_remove_readonly(func, path, exc):
    os.chmod(path, stat.S_IRWXU| stat.S_IRWXG| stat.S_IRWXO) # 0777
    func(path)

extension_args = {}
Options.language_level = 3

# If building Synthizer itself, set additional include and lib directories to the root of the Synthizer repository.
# We build this against something like 10 Python versions, and cloning the repo for all of them isn't workable. Also, Windows is a bit flaky,
# so we publish wheels that are manually orchestrated from outside this file.
if 'BUILDING_SYNTHIZER' in os.environ:
    print("Building Synthizer from repository. Adding additional directories.")
    repo_root = os.path.abspath(os.path.join(os.path.split(os.path.abspath(__file__))[0], "../.."))
    print("Repository root is", repo_root)
    if "SYNTHIZER_CI" in os.environ:
        synthizer_build_dir = os.path.join(repo_root, "build_static_release")
        synthizer_lib = "synthizer_static"
    else:
        synthizer_build_dir = os.path.join(repo_root, "build")
        synthizer_lib = "synthizer"
    synthizer_include_dir = os.path.    join(repo_root, "include")
    extension_args = {
        'include_dirs': [synthizer_include_dir],
        'library_dirs': [synthizer_build_dir],
        'libraries': [synthizer_lib],
    }
else:
    # scikit-build isn't always present.
    from skbuild import cmaker
    # we can go via scikit-build to try to run CMake. This is reliable enough on Linux and Mac, but will probably 
    # give people trouble on Windows, thus us publishing wheels.
    # Find this file:
    root_dir = os.path.split(os.path.abspath(__file__))[0]
    git_dir = os.path.join(root_dir, "synthizer-git-repo")
    # Be sure we're going to clone next to setup.py.
    os.chdir(root_dir)
    # Always start clean
    if os.path.exists(git_dir):
        shutil.rmtree(git_dir, onerror=handle_remove_readonly)
    # try cloning the Synthizer repository.
    subprocess.check_call(["git", "clone", "https://github.com/synthizer/synthizer", "synthizer-git-repo", "--branch", VERSION])
    # Configure and build Synthizer itself.
    cmake = cmaker.CMaker()
    # Force Ninja on all platforms. This lets us work on Windows reliably if run from an MSVC shell, wher reliably
    # means fail noisily and obviously at the beginning if the world is insane.
    cmake.configure(cmake_source_dir=git_dir, generator_name='Ninja', clargs=["-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded", "-DCMAKE_POSITION_INDEPENDENT_CODE=TRUE"])
    cmake.make()
    # what actually happens here is that Synthizer only installs one path. It is unfortunately the case that
    # skbuild is making architecture specific directories so, instead of guessing, just grab it.
    synthizer_lib_dir = os.path.split(os.path.abspath(cmake.install()[0]))[0]
    extension_args = {
        'include_dirs': [os.path.join(git_dir, "include")],
        'library_dirs': [synthizer_lib_dir],
        'libraries': ['synthizer'],
    }

extensions = [
    Extension("synthizer", ["synthizer/synthizer.pyx"], **extension_args),
]

setup(
    name = "synthizer",
    version = VERSION,
    ext_modules = cythonize(extensions, language_level=3),
    zip_safe = False,
    package_data = {
        'synthizer': ['*.pyx', '*.pxd'],
    }
)
