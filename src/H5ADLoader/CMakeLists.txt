# -----------------------------------------------------------------------------
# H5AD Loader
# -----------------------------------------------------------------------------
set(H5AD_ACTIONS
	actions/TargetElementTypeAction.h
	actions/TargetElementTypeAction.cpp
	actions/ObsmMatrixConfigurationAction.h
	actions/ObsmMatrixConfigurationAction.cpp
	actions/ObsmMatrixSelectionAction.h
	actions/ObsmMatrixSelectionAction.cpp
)

set(H5AD_SOURCES
	H5ADUtils.h
	H5ADUtils.cpp
	H5ADImportDialog.h
	H5ADImportDialog.cpp
	${H5AD_ACTIONS}
	HDF5_AD_Loader.h
	HDF5_AD_Loader.cpp

	H5ADLoader.h
	H5ADLoader.cpp
	PluginInfo.json

	H5ADLoaderFactory.h
	H5ADLoaderFactory.cpp
)

source_group(H5ADLoader FILES
	H5ADUtils.h
	H5ADUtils.cpp
	H5ADImportDialog.h
	H5ADImportDialog.cpp
	HDF5_AD_Loader.h
	HDF5_AD_Loader.cpp
	H5ADLoader.h
	H5ADLoader.cpp
	PluginInfo.json
	H5ADLoaderFactory.h
	H5ADLoaderFactory.cpp
)
source_group(Actions FILES
	${CMAKE_CURRENT_SOURCE_DIR}/actions/TargetElementTypeAction.h
	${CMAKE_CURRENT_SOURCE_DIR}/actions/TargetElementTypeAction.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/actions/ObsmMatrixConfigurationAction.h
	${CMAKE_CURRENT_SOURCE_DIR}/actions/ObsmMatrixConfigurationAction.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/actions/ObsmMatrixSelectionAction.h
	${CMAKE_CURRENT_SOURCE_DIR}/actions/ObsmMatrixSelectionAction.cpp
)
source_group(HFD5_Common FILES ${SHARED_SOURCES} ${SHARED_HEADERS})

add_library(${H5ADPROJECT} SHARED ${H5AD_SOURCES} ${SHARED_SOURCES} ${SHARED_HEADERS})

# -----------------------------------------------------------------------------
# Target properties and linking
# -----------------------------------------------------------------------------
SetBuildSettings(${H5ADPROJECT})
SetPluginSettings(${H5ADPROJECT})
SetDebugEnvironment(${H5ADPROJECT})
