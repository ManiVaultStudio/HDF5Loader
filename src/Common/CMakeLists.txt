# -----------------------------------------------------------------------------
# Common source files
# -----------------------------------------------------------------------------

set(COMMON_HDF5_DIR 
    ${CMAKE_CURRENT_SOURCE_DIR}
    CACHE INTERNAL "Common include and source directory"
)

set(DATATRANSFORM_SOURCES
	${COMMON_HDF5_DIR}/DataTransform.cpp
	${COMMON_HDF5_DIR}/DataTransform.h
    CACHE INTERNAL "Common datatransform sources"
)

set(SHARED_SOURCES
	${COMMON_HDF5_DIR}/DataContainerInterface.cpp
	${COMMON_HDF5_DIR}/H5Utils.cpp
    CACHE INTERNAL "Common sources"
)

set(SHARED_HEADERS
	${COMMON_HDF5_DIR}/DataContainerInterface.h
	${COMMON_HDF5_DIR}/H5Utils.h
	${COMMON_HDF5_DIR}/VectorHolder.h
    CACHE INTERNAL "Common headers"
)
