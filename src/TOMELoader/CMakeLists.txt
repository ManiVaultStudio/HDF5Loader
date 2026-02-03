# -----------------------------------------------------------------------------
# TOME Loader
# -----------------------------------------------------------------------------
set(TOME_SOURCES
	HDF5_TOME_Loader.h
	HDF5_TOME_Loader.cpp

	TOMELoader.h
	TOMELoader.cpp
	PluginInfo.json

	TOMELoaderFactory.h
	TOMELoaderFactory.cpp
)

source_group( TOMELoader FILES ${TOME_SOURCES})
add_library(${TOMEPROJECT} SHARED ${TOME_SOURCES} ${SHARED_SOURCES} ${SHARED_HEADERS} ${DATATRANSFORM_SOURCES})

# -----------------------------------------------------------------------------
# Target properties and linking
# -----------------------------------------------------------------------------
SetBuildSettings(${TOMEPROJECT})
SetPluginSettings(${TOMEPROJECT})
SetDebugEnvironment(${TOMEPROJECT})
