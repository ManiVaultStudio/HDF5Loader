cmake_minimum_required(VERSION 3.24.0)
message(STATUS "Building with cmake version ${CMAKE_VERSION}")

option(USE_HDF5_ARTIFACTORY_LIBS "Use the prebuilt libraries from artifactory" ON)

# -----------------------------------------------------------------------------
# HDF5 Loader Plugin
# -----------------------------------------------------------------------------
set(LOADERFOLDER "LoaderPlugins/HDF5")
set(PROJECT "HDF5Loader")
set(H510XPROJECT "H510XLoader")
set(H5ADPROJECT "H5ADLoader")
set(TOMEPROJECT "TOMELoader")

PROJECT(${PROJECT})

# -----------------------------------------------------------------------------
# CMake Options
# -----------------------------------------------------------------------------
SET_PROPERTY(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

if(NOT DEFINED MV_H5_USE_VCPKG)
    set(MV_H5_USE_VCPKG OFF)
elseif(MV_H5_USE_VCPKG)
    message(STATUS "HDF5 Loader Plugins: Using vcpkg to install dependencies")
endif()

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------
find_package(Qt6 COMPONENTS Widgets WebEngineWidgets REQUIRED)
find_package(OpenMP REQUIRED)

find_package(ManiVault COMPONENTS Core PointData ClusterData CONFIG QUIET)

if(MV_H5_USE_VCPKG)
    find_package(hdf5 CONFIG REQUIRED COMPONENTS CXX HL)
    message(STATUS "Found HDF5 with version ${hdf5_VERSION}")
    set(USE_HDF5_ARTIFACTORY_LIBS OFF)
else()
    include(HDF5Dependency)
endif()

if(MSVC)
	message(STATUS "HDF5 Loader: including MSVC settings")
	include(MSVCSettings)
endif()

# -----------------------------------------------------------------------------
# Source files and CMake targets
# -----------------------------------------------------------------------------
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

# -----------------------------------------------------------------------------
# Target include directories, linking and properties
# -----------------------------------------------------------------------------
function(SetBuildSettings PROJNAME)

	target_link_libraries(${PROJNAME} PRIVATE OpenMP::OpenMP_CXX)
	target_compile_options(${PROJNAME} PRIVATE ${OpenMP_CXX_FLAGS})

	target_compile_features(${PROJNAME} PRIVATE cxx_std_20)
	set_target_properties(${PROJNAME} PROPERTIES CXX_STANDARD 20)
	target_compile_definitions(${PROJNAME} PRIVATE BIOVAULT_BFLOAT16_CONVERTING_CONSTRUCTORS)

    if(MV_H5_USE_VCPKG)
        target_link_libraries (${PROJNAME} PRIVATE hdf5::hdf5_cpp-static hdf5::hdf5_hl_cpp-static)
    elseif(${USE_HDF5_ARTIFACTORY_LIBS})
        message(STATUS "Linking with artifactory libraries ${HDF5_CXX_STATIC_LIBRARY} and ${HDF5_ZLIB_STATIC}")
        target_include_directories("${PROJNAME}" PRIVATE "${HDF5_INCLUDE_DIR}")
        target_include_directories("${PROJNAME}" PRIVATE "${LIBRARY_INSTALL_DIR}/zlib/$<CONFIG>/include")

        target_link_libraries (${PROJNAME} PRIVATE ${HDF5_CXX_STATIC_LIBRARY} ${HDF5_ZLIB_STATIC})

    else()
        message(STATUS "Linking with local (externalProject) ${HDF5_CXX_STATIC_LIBRARY} and ${HDF5_ZLIB_STATIC}")
        add_dependencies(${PROJNAME} hdf5)

        file(GENERATE OUTPUT hdf5_linking_files_${PROJNAME}_$<CONFIG>.txt CONTENT ${HDF5_C_STATIC_LIBRARY})
        message(STATUS "Linking with   ${HDF5_CXX_STATIC_LIBRARY} and ${ZLIB_LIBRARIES}")
        target_include_directories(${PROJNAME} PRIVATE ${HDF5_INCLUDE_DIR})
        target_link_libraries(${PROJNAME} PRIVATE ${HDF5_C_STATIC_LIBRARY} ${HDF5_CXX_STATIC_LIBRARY} ${ZLIB_LIBRARIES})

    endif()

	target_link_libraries(${PROJNAME} PRIVATE Qt6::Widgets)
	target_link_libraries(${PROJNAME} PRIVATE Qt6::WebEngineWidgets)

	target_include_directories(${PROJNAME} PRIVATE "${ManiVault_INCLUDE_DIR}")
	target_link_libraries(${PROJNAME} PRIVATE ManiVault::Core)
	target_link_libraries(${PROJNAME} PRIVATE ManiVault::PointData)
	target_link_libraries(${PROJNAME} PRIVATE ManiVault::ClusterData)

	if(MSVC)
        target_link_libraries(${PROJNAME} PRIVATE shlwapi.lib)
	endif(MSVC)

endfunction()

# -----------------------------------------------------------------------------
# Target installation
# -----------------------------------------------------------------------------
function(SetPluginSettings PROJNAME)
	install(TARGETS ${PROJNAME}
		RUNTIME DESTINATION Plugins COMPONENT ${PROJNAME} # Windows .dll
		LIBRARY DESTINATION Plugins COMPONENT ${PROJNAME} # Linux/Mac .so
	)
	
	add_custom_command(TARGET ${PROJNAME} POST_BUILD
		COMMAND "${CMAKE_COMMAND}"
		--install ${CMAKE_CURRENT_BINARY_DIR}
		--config $<CONFIG>
		--prefix ${ManiVault_INSTALL_DIR}/$<CONFIG>
		--component ${PROJNAME}
	)
endfunction()

# -----------------------------------------------------------------------------
# Setup all targets
# -----------------------------------------------------------------------------
SetBuildSettings(${H5ADPROJECT})
SetBuildSettings(${H510XPROJECT})
SetBuildSettings(${TOMEPROJECT})

SetPluginSettings(${H5ADPROJECT})
SetPluginSettings(${H510XPROJECT})
SetPluginSettings(${TOMEPROJECT})

# -----------------------------------------------------------------------------
# Miscellaneous
# -----------------------------------------------------------------------------
# Automatically set the debug environment (command + working directory) for MSVC
if(MSVC)
    set_property(TARGET ${H5ADPROJECT} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<IF:$<CONFIG:DEBUG>,${ManiVault_INSTALL_DIR}/Debug,$<IF:$<CONFIG:RELWITHDEBINFO>,${ManiVault_INSTALL_DIR}/RelWithDebInfo,${ManiVault_INSTALL_DIR}/Release>>)
    set_property(TARGET ${H5ADPROJECT} PROPERTY VS_DEBUGGER_COMMAND $<IF:$<CONFIG:DEBUG>,"${ManiVault_INSTALL_DIR}/Debug/ManiVault Studio.exe",$<IF:$<CONFIG:RELWITHDEBINFO>,"${ManiVault_INSTALL_DIR}/RelWithDebInfo/ManiVault Studio.exe","${ManiVault_INSTALL_DIR}/Release/ManiVault Studio.exe">>)

    set_property(TARGET ${H510XPROJECT} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<IF:$<CONFIG:DEBUG>,${ManiVault_INSTALL_DIR}/Debug,$<IF:$<CONFIG:RELWITHDEBINFO>,${ManiVault_INSTALL_DIR}/RelWithDebInfo,${ManiVault_INSTALL_DIR}/Release>>)
    set_property(TARGET ${H510XPROJECT} PROPERTY VS_DEBUGGER_COMMAND $<IF:$<CONFIG:DEBUG>,"${ManiVault_INSTALL_DIR}/Debug/ManiVault Studio.exe",$<IF:$<CONFIG:RELWITHDEBINFO>,"${ManiVault_INSTALL_DIR}/RelWithDebInfo/ManiVault Studio.exe","${ManiVault_INSTALL_DIR}/Release/ManiVault Studio.exe">>)

    set_property(TARGET ${TOMEPROJECT} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<IF:$<CONFIG:DEBUG>,${ManiVault_INSTALL_DIR}/Debug,$<IF:$<CONFIG:RELWITHDEBINFO>,${ManiVault_INSTALL_DIR}/RelWithDebInfo,${ManiVault_INSTALL_DIR}/Release>>)
    set_property(TARGET ${TOMEPROJECT} PROPERTY VS_DEBUGGER_COMMAND $<IF:$<CONFIG:DEBUG>,"${ManiVault_INSTALL_DIR}/Debug/ManiVault Studio.exe",$<IF:$<CONFIG:RELWITHDEBINFO>,"${ManiVault_INSTALL_DIR}/RelWithDebInfo/ManiVault Studio.exe","${ManiVault_INSTALL_DIR}/Release/ManiVault Studio.exe">>)
endif()
