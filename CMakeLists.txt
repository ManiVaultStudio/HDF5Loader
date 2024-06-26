cmake_minimum_required(VERSION 3.24.0)
message(STATUS "Building with cmake version ${CMAKE_VERSION}")

set(LOADERFOLDER "HDF5Loaders")
set(PROJECT "HDF5Loader")
set(H510XPROJECT "H510XLoader")
set(H5ADPROJECT "H5ADLoader")
set(TOMEPROJECT "TOMELoader")

PROJECT(${PROJECT})

SET_PROPERTY(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

# -----------------------------------------------------------------------------
# Library versions
# -----------------------------------------------------------------------------
SET(MANIVAULT_API_VERSION "New" CACHE STRING "ManiVault API Version")
SET_PROPERTY(CACHE MANIVAULT_API_VERSION PROPERTY STRINGS Old New)
add_compile_definitions(MANIVAULT_API_${MANIVAULT_API_VERSION})

include(HDF5Dependency)
message(STATUS "Msvc settings")
include(MSVCSettings)

if (APPLE)
	# set(BREW_PATH "brew" CACHE FILEPATH "the path to the brew that matches the target build architecture (m1 or x86_64" )
	# execute_process(COMMAND ${BREW_PATH} --prefix libomp RESULT_VARIABLE LIBOMP_ROOT)
	# In CI OpenMP_ROOT is set by conan
	if(NOT DEFINED OpenMP_ROOT)
		set(OpenMP_ROOT "/usr/local/homebrew/opt/libomp")
	endif()
endif ()

# Check if the directory to the ManiVault installation has been provided
if(NOT DEFINED MV_INSTALL_DIR)
    set(MV_INSTALL_DIR "" CACHE PATH "Directory where ManiVault is installed")
    message(FATAL_ERROR "Please set MV_INSTALL_DIR to the directory where ManiVault is installed")
endif()
file(TO_CMAKE_PATH ${MV_INSTALL_DIR} MV_INSTALL_DIR)

find_package(Qt6 COMPONENTS Widgets WebEngineWidgets REQUIRED)

set(DATATRANSFORM_SOURCES
	src/DataTransform.cpp	
	src/DataTransform.h
)

set(SHARED_SOURCES
	src/DataContainerInterface.cpp	
	src/H5Utils.cpp	
)

set(SHARED_HEADERS
	src/DataContainerInterface.h
	src/H5Utils.h
	
	src/VectorHolder.h
)

source_group(${PROJECT} FILES ${SHARED_HEADERS} ${SHARED_SOURCES} ${DATATRANSFORM_SOURCES})

## BEGIN H510X Loader

set(H510X_SOURCES
	src/HDF5_10X_Loader.h
	src/HDF5_10X_Loader.cpp

	src/H510XLoader.h
	src/H510XLoader.cpp
	src/H510XLoader.json

	src/H510XLoaderFactory.h
	src/H510XLoaderFactory.cpp
)

source_group(H510XLoader FILES ${H510X_SOURCES})
add_library(${H510XPROJECT} SHARED ${H510X_SOURCES} ${SHARED_SOURCES} ${SHARED_HEADERS} ${DATATRANSFORM_SOURCES})

SET_PROPERTY(TARGET ${H510XPROJECT} PROPERTY FOLDER ${LOADERFOLDER})
## END H510X Loader

## BEGIN H5AD Loader
set(H5AD_SOURCES

	src/H5ADUtils.h
	src/H5ADUtils.cpp
	src/HDF5_AD_Loader.h
	src/HDF5_AD_Loader.cpp

	src/H5ADLoader.h
	src/H5ADLoader.cpp
	src/H5ADLoader.json

	src/H5ADLoaderFactory.h
	src/H5ADLoaderFactory.cpp
	
)

source_group( H5ADLoader FILES ${H5AD_SOURCES})
add_library(${H5ADPROJECT} SHARED ${H5AD_SOURCES} ${SHARED_SOURCES} ${SHARED_HEADERS})

SET_PROPERTY(TARGET ${H5ADPROJECT} PROPERTY FOLDER ${LOADERFOLDER})
## END H5AD Loader

## BEGIN TOME Loader
set(TOME_SOURCES
	src/HDF5_TOME_Loader.h
	src/HDF5_TOME_Loader.cpp

	src/TOMELoader.h
	src/TOMELoader.cpp
	src/TOMELoader.json

	src/TOMELoaderFactory.h
	src/TOMELoaderFactory.cpp
)

source_group( TOMELoader FILES ${TOME_SOURCES})
add_library(${TOMEPROJECT} SHARED ${TOME_SOURCES} ${SHARED_SOURCES} ${SHARED_HEADERS} ${DATATRANSFORM_SOURCES})

SET_PROPERTY(TARGET ${TOMEPROJECT} PROPERTY FOLDER ${LOADERFOLDER})
## END TOME Loader

set(MV_LINK_PATH "${MV_INSTALL_DIR}/$<CONFIG>/lib")
set(PLUGIN_LINK_PATH "${MV_INSTALL_DIR}/$<CONFIG>/$<IF:$<CXX_COMPILER_ID:MSVC>,lib,Plugins>")
set(MV_LINK_SUFFIX $<IF:$<CXX_COMPILER_ID:MSVC>,${CMAKE_LINK_LIBRARY_SUFFIX},${CMAKE_SHARED_LIBRARY_SUFFIX}>)

set(MV_LINK_LIBRARY "${MV_LINK_PATH}/${CMAKE_SHARED_LIBRARY_PREFIX}MV_Public${MV_LINK_SUFFIX}")
set(POINTDATA_LINK_LIBRARY "${PLUGIN_LINK_PATH}/${CMAKE_SHARED_LIBRARY_PREFIX}PointData${MV_LINK_SUFFIX}") 
set(CLUSTERDATA_LINK_LIBRARY "${PLUGIN_LINK_PATH}/${CMAKE_SHARED_LIBRARY_PREFIX}ClusterData${MV_LINK_SUFFIX}") 

message(STATUS ${SOURCE_DIR})
message(STATUS ${BINARY_DIR})

function(SetBuildSettings PROJNAME)

	target_include_directories(${PROJNAME} PRIVATE ${HDF5_INCLUDE_DIR})
	target_include_directories(${PROJNAME} PRIVATE "${MV_INSTALL_DIR}/$<CONFIG>/include/")

	find_package(OpenMP REQUIRED)
	if(OpenMP_FOUND)
		message(STATUS "OpenMP found")
		target_link_libraries(${PROJNAME} OpenMP::OpenMP_CXX)
		target_compile_options(${PROJNAME} PRIVATE ${OpenMP_CXX_FLAGS})
	else()
	    message(WARNING "OpenMP not found!")
	endif()

	target_compile_features(${PROJNAME} PRIVATE cxx_std_17)
	set_target_properties(${PROJNAME} PROPERTIES CXX_STANDARD 17)

	if (NOT USE_HDF5_ARTIFACTORY_LIBS)
		add_dependencies(${PROJNAME} hdf5)
	else()
		target_include_directories("${PROJNAME}" PRIVATE "${HDF5_INCLUDE_DIR}")
		target_include_directories("${PROJNAME}" PRIVATE "${LIBRARY_INSTALL_DIR}/zlib/$<CONFIG>/include")
		target_include_directories("${PROJNAME}" PRIVATE "${MV_INSTALL_DIR}/$<CONFIG>/include/")
	endif()

	target_compile_definitions(${PROJNAME} PRIVATE BIOVAULT_BFLOAT16_CONVERTING_CONSTRUCTORS)
	target_link_libraries(${PROJNAME} Qt6::Widgets)
	target_link_libraries(${PROJNAME} Qt6::WebEngineWidgets)
	target_link_libraries(${PROJNAME} "${MV_LINK_LIBRARY}")
	target_link_libraries(${PROJNAME} "${POINTDATA_LINK_LIBRARY}")
	target_link_libraries(${PROJNAME} "${CLUSTERDATA_LINK_LIBRARY}")

	message(status "************Define target linkage***********")
	if (NOT USE_HDF5_ARTIFACTORY_LIBS)
	    file(GENERATE OUTPUT hdf5_linking_files_${PROJNAME}_$<CONFIG>.txt CONTENT ${HDF5_C_STATIC_LIBRARY})
		message(STATUS "Linking with   ${HDF5_CXX_STATIC_LIBRARY} and ${ZLIB_LIBRARIES}")
		target_link_libraries(${PROJNAME} ${HDF5_C_STATIC_LIBRARY} ${HDF5_CXX_STATIC_LIBRARY} ${ZLIB_LIBRARIES})
	else()
		message(STATUS "Linking with ${HDF5_CXX_STATIC_LIBRARY} and ${HDF5_ZLIB_STATIC}")  
		target_link_libraries (${PROJNAME} ${HDF5_CXX_STATIC_LIBRARY} ${HDF5_ZLIB_STATIC}) 
	endif()


	if(MSVC)
	target_link_libraries(${PROJNAME} shlwapi.lib)
	endif(MSVC)
	
	

endfunction()

function(SetPluginSettings PROJNAME)
#	add_dependencies(${PROJNAME} ${PROJECT})
#	target_link_libraries(${PROJNAME} ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT}${CMAKE_STATIC_LIBRARY_SUFFIX})

	install(TARGETS ${PROJNAME}
		RUNTIME DESTINATION Plugins COMPONENT ${PROJNAME} # Windows .dll
		LIBRARY DESTINATION Plugins COMPONENT ${PROJNAME} # Linux/Mac .so
		
	)
	

	add_custom_command(TARGET ${PROJNAME} POST_BUILD
		COMMAND "${CMAKE_COMMAND}"
		--install ${CMAKE_CURRENT_BINARY_DIR}
		--config $<CONFIG>
		--prefix ${MV_INSTALL_DIR}/$<CONFIG>
		--component ${PROJNAME}
	)
endfunction()


#SetBuildSettings(${PROJECT})
SetBuildSettings(${H5ADPROJECT})
SetBuildSettings(${H510XPROJECT})
SetBuildSettings(${TOMEPROJECT})

SetPluginSettings(${H5ADPROJECT})
SetPluginSettings(${H510XPROJECT})
SetPluginSettings(${TOMEPROJECT})

# Automatically set the debug environment (command + working directory) for MSVC
	if(MSVC)
		set_property(TARGET ${H510XPROJECT} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<IF:$<CONFIG:DEBUG>,${MV_INSTALL_DIR}/debug,${MV_INSTALL_DIR}/release>)
		set_property(TARGET ${H510XPROJECT} PROPERTY VS_DEBUGGER_COMMAND $<IF:$<CONFIG:DEBUG>,"${MV_INSTALL_DIR}/debug/ManiVault Studio.exe","${MV_INSTALL_DIR}/release/ManiVault Studio.exe">)
	endif()
