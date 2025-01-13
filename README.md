# HDF5Loader
Loader plugins for [HDF5](https://en.wikipedia.org/wiki/Hierarchical_Data_Format) based formats such as 10X, TOME and H5AD.

This repo currently builds three plugins:
- H510XLoader
- H5ADLoader
- TOMELoader

## HDF5 dependency
By default, a pre-built HDF5 library will be downloaded from the LKEB artifactory during cmake's configuration step.

You can also install HDF5 with [vcpkg](https://github.com/microsoft/vcpkg) and use `-DCMAKE_TOOLCHAIN_FILE="[YOURPATHTO]/vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static-md` to point to your vcpkg installation:
```bash
./vcpkg install hdf5[cpp,zlib]:x64-windows-static-md
```
Depending on your OS the `VCPKG_TARGET_TRIPLET` might vary, e.g. for linux you probably don't need to specify any since it automatically builds static libraries.

If not providing a vcpkg toolchain file but setting `USE_HDF5_ARTIFACTORY_LIBS` to `OFF`, cmake will automatically download hdf5 [form it's GitHub repo](https://github.com/HDFGroup/hdf5) and build it locally.
