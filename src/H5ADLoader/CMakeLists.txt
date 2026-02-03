# -----------------------------------------------------------------------------
# H5AD Loader
# -----------------------------------------------------------------------------
set(H5AD_SOURCES
	H5ADUtils.h
	H5ADUtils.cpp
	HDF5_AD_Loader.h
	HDF5_AD_Loader.cpp

	H5ADLoader.h
	H5ADLoader.cpp
	PluginInfo.json

	H5ADLoaderFactory.h
	H5ADLoaderFactory.cpp
)

source_group(H5ADLoader FILES ${H5AD_SOURCES})
source_group(HFD5_Common FILES ${SHARED_SOURCES} ${SHARED_HEADERS})

add_library(${H5ADPROJECT} SHARED ${H5AD_SOURCES} ${SHARED_SOURCES} ${SHARED_HEADERS})

# -----------------------------------------------------------------------------
# Target properties and linking
# -----------------------------------------------------------------------------
SetBuildSettings(${H5ADPROJECT})
SetPluginSettings(${H5ADPROJECT})
SetDebugEnvironment(${H5ADPROJECT})
