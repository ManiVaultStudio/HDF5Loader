# -----------------------------------------------------------------------------
# H510X Loader
# -----------------------------------------------------------------------------
set(H510X_SOURCES
	HDF5_10X_Loader.h
	HDF5_10X_Loader.cpp

	H510XLoader.h
	H510XLoader.cpp
	PluginInfo.json

	H510XLoaderFactory.h
	H510XLoaderFactory.cpp
)

source_group(H510XLoader FILES ${H510X_SOURCES})
add_library(${H510XPROJECT} SHARED ${H510X_SOURCES} ${SHARED_SOURCES} ${SHARED_HEADERS} ${DATATRANSFORM_SOURCES})

# -----------------------------------------------------------------------------
# Target properties and linking
# -----------------------------------------------------------------------------
SetBuildSettings(${H510XPROJECT})
SetPluginSettings(${H510XPROJECT})
SetDebugEnvironment(${H510XPROJECT})
