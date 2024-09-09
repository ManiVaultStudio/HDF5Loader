# HDF5Loader
Loader Plugin for HDF5 based formats such as 10X, TOME and H5AD. 

## With vcpkg
Install the hdf5 library with vcpkg instead of retrieving it from the lkeb artifactory or building it locally:

```bash
./vcpkg install hdf5[cpp,zlib]:x64-windows-static-md
```

Use -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake" to point to your vcpkg installation.
Depending in your OS the `VCPKG_TARGET_TRIPLET` might vary.
