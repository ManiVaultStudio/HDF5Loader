from conans import ConanFile
from conan.tools.cmake import CMakeDeps, CMake, CMakeToolchain
from conans.tools import save, load, os_info, SystemPackageTool
import os
import shutil
import pathlib
import subprocess
from rules_support import PluginBranchInfo
import re

def compatibility(os, compiler, compiler_version):
    # On macos fallback to zlib apple-clang 13
    if os == "Macos" and compiler == "apple-clang" and bool(re.match("14.*", compiler_version)):  
        print("Compatibility match")
        return ["zlib/1.3:compiler.version=13"]
    return None

def compareVersion(version1, version2):
    versions1 = [int(v) for v in version1.split(".")]
    versions2 = [int(v) for v in version2.split(".")]
    for i in range(max(len(versions1), len(versions2))):
        v1 = versions1[i] if i < len(versions1) else 0
        v2 = versions2[i] if i < len(versions2) else 0
        if v1 > v2:
            return 1
        elif v1 < v2:
            return -1
    return 0


class HDF5LoaderConan(ConanFile):
    """Class to package the HDF5Loader plugin using conan

    Packages both RELEASE and DEBUG.
    Uses rules_support (github.com/hdps/rulessupport) to derive
    versioninfo based on the branch naming convention
    as described in https://github.com/hdps/core/wiki/Branch-naming-rules
    """

    name = "HDF5LoaderPlugin"
    description = (
        "Load HDF5 encoded data into the HDPS framework. " "Supports 10x TOME and H5AD"
    )
    topics = ("hdps", "plugin", "data", "HDF5 loader")
    url = "https://github.com/hdps/HDF5Loader"
    author = "B. van Lew b.van_lew@lumc.nl"  # conan recipe author
    license = "MIT"  # conan recipe license

    short_paths = True
    generators = "CMakeDeps"

    # Options may need to change depending on the packaged library
    settings = {"os": None, "build_type": None, "compiler": None, "arch": None}
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": True, "fPIC": True}

    #   QT requirement is provided by hdps-core "qt/5.15.1@lkeb/stable",

    scm = {
        "type": "git",
        "subfolder": "hdps/HDF5Loader",
        "url": "auto",
        "revision": "auto",
    }

    def __get_git_path(self):
        path = load(
            pathlib.Path(pathlib.Path(__file__).parent.resolve(), "__gitpath.txt")
        )
        print(f"git info from {path}")
        return path

    def export(self):
        print("In export")
        # save the original source path to the directory used to build the package
        save(
            pathlib.Path(self.export_folder, "__gitpath.txt"),
            str(pathlib.Path(__file__).parent.resolve()),
        )

    def set_version(self):
        # Assign a version from the branch name
        branch_info = PluginBranchInfo(self.recipe_folder)
        self.version = branch_info.version

    def requirements(self):
        branch_info = PluginBranchInfo(self.__get_git_path())
        print(f"Core requirement {branch_info.core_requirement}")
        self.requires(branch_info.core_requirement)

    def configure(self):
        pass

    def system_requirements(self):
        if os_info.is_macos:
            installer = SystemPackageTool()
            installer.install("libomp")
            proc = subprocess.run("brew --prefix libomp",  shell=True, capture_output=True)
            subprocess.run(f"ln {proc.stdout.decode('UTF-8').strip()}/lib/libomp.dylib /usr/local/lib/libomp.dylib", shell=True)

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def generate(self):
        generator = None
        if self.settings.os == "Macos":
            generator = "Xcode"
        if self.settings.os == "Linux":
            generator = "Ninja Multi-Config"

        tc = CMakeToolchain(self, generator=generator)
        if self.settings.os == "Windows" and self.options.shared:
            tc.variables["CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS"] = True
        if self.settings.os == "Linux" or self.settings.os == "Macos":
            tc.variables["CMAKE_CXX_STANDARD_REQUIRED"] = "ON"
            
        # Use the Qt provided .cmake files    
        qtpath = pathlib.Path(self.deps_cpp_info["qt"].rootpath)
        qt_root = str(list(qtpath.glob("**/Qt6Config.cmake"))[0].parents[3].as_posix())
        tc.variables["Qt6_ROOT"] = f"{qt_root}"

        if os_info.is_macos:
            proc = subprocess.run(
                "brew --prefix libomp", shell=True, capture_output=True
            )
            prefix_path = f"{proc.stdout.decode('UTF-8').strip()}"
            tc.variables["OpenMP_ROOT"] = prefix_path
            
        tc.variables["USE_HDF5_ARTIFACTORY_LIBS"] = "ON"
        tc.variables[
            "CMAKE_BUILD_PARALLEL_LEVEL"
        ] = "1"  # Try single core build for out of memory problems
        
        # Set the installation directory for ManiVault based on the MV_INSTALL_DIR environment variable
        # or if none is specified, set it to the build/install dir.
        if not os.environ.get("MV_INSTALL_DIR", None):
            os.environ["MV_INSTALL_DIR"] = os.path.join(self.build_folder, "install")
        print("MV_INSTALL_DIR: ", os.environ["MV_INSTALL_DIR"])
        self.install_dir = pathlib.Path(os.environ["MV_INSTALL_DIR"]).as_posix()
        # Give the installation directory to CMake
        tc.variables["MV_INSTALL_DIR"] = self.install_dir
        
        # Find ManiVault with find_package
        self.manivault_dir = self.install_dir + '/cmake/mv/'
        tc.variables["ManiVault_DIR"] = self.manivault_dir
        
        tc.generate()

    def _configure_cmake(self):
        cmake = CMake(self)
        cmake.verbose = True
        cmake.configure(build_script_folder="hdps/HDF5Loader")
        return cmake

    def build(self):
        print("Build OS is : ", self.settings.os)

        # The HDF5Loader build expects the HDPS package to be in this install dir
        hdps_pkg_root = self.deps_cpp_info["hdps-core"].rootpath
        print("Install dir type: ", self.install_dir)
        shutil.copytree(hdps_pkg_root, self.install_dir)

        cmake = self._configure_cmake()
        cmake.build(build_type="Debug", cli_args=["--verbose"])
        cmake.install(build_type="Debug")

        cmake_release = self._configure_cmake()
        cmake_release.build(build_type="Release")
        cmake_release.install(build_type="Release")

    def package(self):
        package_dir = os.path.join(self.build_folder, "package")
        print("Packaging install dir: ", package_dir)
        subprocess.run(
           [
               "cmake",
               "--install",
               self.build_folder,
               "--config",
               "Debug",
               "--prefix",
               os.path.join(package_dir, "Debug"),
           ]
        )
        subprocess.run(
            [
                "cmake",
                "--install",
                self.build_folder,
                "--config",
                "Release",
                "--prefix",
                os.path.join(package_dir, "Release"),
            ]
        )
        self.copy(pattern="*", src=package_dir)

    def package_info(self):
        self.cpp_info.debug.libdirs = ["Debug/lib"]
        self.cpp_info.debug.bindirs = ["Debug/Plugins", "Debug"]
        self.cpp_info.debug.includedirs = ["Debug/include", "Debug"]
        self.cpp_info.release.libdirs = ["Release/lib"]
        self.cpp_info.release.bindirs = ["Release/Plugins", "Release"]
        self.cpp_info.release.includedirs = ["Release/include", "Release"]
