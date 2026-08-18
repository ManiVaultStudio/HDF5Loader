cmake_minimum_required(VERSION 3.24.0)

# -----------------------------------------------------------------------------
# User options
# -----------------------------------------------------------------------------
option(USE_HDF5_ARTIFACTORY_LIBS "Use the prebuilt libraries from artifactory" ON)

if(NOT DEFINED MV_H5_USE_VCPKG)
    set(MV_H5_USE_VCPKG OFF)
elseif(MV_H5_USE_VCPKG)
    message(STATUS "HDF5 Loader Plugins: Using vcpkg to install dependencies")
endif()

# -----------------------------------------------------------------------------
# HDF5 Loader Plugin
# -----------------------------------------------------------------------------
set(PROJECT "HDF5Loader")
set(H510XPROJECT "H510XLoader")
set(H5ADPROJECT "H5ADLoader")
set(TOMEPROJECT "TOMELoader")

PROJECT(${PROJECT}
        DESCRIPTION "Loader Plugin for HDF5 based formats for ManiVault"
        LANGUAGES CXX
)

# -----------------------------------------------------------------------------
# CMake Options
# -----------------------------------------------------------------------------
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /DWIN32 /EHsc /MP /permissive- /Zc:__cplusplus")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /MD")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD")	

    if(PROJECT_IS_TOP_LEVEL)
        set(CMAKE_CXX_FLAGS_RELEASE_INIT "/GL")
        set(CMAKE_SHARED_LINKER_FLAGS_INIT "/LTCG:PGOptimize")
    endif()
	
	cmake_host_system_information(RESULT TOTAL_PHYSICAL_MEMORY QUERY TOTAL_PHYSICAL_MEMORY)

	if(${TOTAL_PHYSICAL_MEMORY} LESS 16385)
		message(STATUS "Disabling Inline Function Expansion as available Memory = ${TOTAL_PHYSICAL_MEMORY} < 16385")
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Ob0")
	endif()
endif()

# handle linking of each target the same
include(HDF5Utils)

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

# -----------------------------------------------------------------------------
# Project targets
# -----------------------------------------------------------------------------
add_subdirectory(src/Common)
add_subdirectory(src/H5ADLoader)
add_subdirectory(src/H510XLoader)
add_subdirectory(src/TOMELoader)
