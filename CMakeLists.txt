cmake_minimum_required(VERSION 3.1.0)

set(PROJECT "HDF5Loader")

PROJECT(${PROJECT})

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /DWIN32 /EHsc /MP")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:LIBCMT")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD")
endif(MSVC)

find_package(Qt5 5.12 COMPONENTS Widgets WebEngineWidgets REQUIRED)


# Het HDF5 with ZLIB
include(ExternalProject)

set(HDF5_PREFIX hdf5-1-10)

set(git_stash_save_options --all --quiet)



if(WIN32)
ExternalProject_Add(hdf5
GIT_REPOSITORY https://bitbucket.hdfgroup.org/scm/hdffv/hdf5.git
GIT_TAG 1.10/master
        GIT_SHALLOW ON
PREFIX ${HDF5_PREFIX}
CMAKE_ARGS
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
 -DBUILD_SHARED_LIBS=OFF
 -DBUILD_TESTING=OFF 
 -DHDF5_BUILD_EXAMPLES=OFF 
 -DHDF5_BUILD_HL_LIB=OFF 
 -DHDF5_BUILD_TOOLS=OFF 
 -DHDF5_ENABLE_EMBEDDED_LIBINFO=OFF 
 -DHDF5_ENABLE_HSIZET=OFF 
 -DHDF5_ALLOW_EXTERNAL_SUPPORT="GIT" 
 -DHDF5_ENABLE_Z_LIB_SUPPORT=ON
 -DZLIB_URL=https://github.com/madler/zlib 
 -DZLIB_BRANCH=master
BUILD_COMMAND ${INCREDIBUILD_COMMAND}
INSTALL_COMMAND ""
#	${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/$<CONFIG>/$<$<CONFIG:Debug>:libhdf5_D.lib>$<$<CONFIG:Release>:libhdf5.lib>" "${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/$<CONFIG>/$<$<CONFIG:Debug>:libhdf5_cpp_D.lib>$<$<CONFIG:Release>:libhdf5_cpp.lib>" "${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/$<CONFIG>/$<$<CONFIG:Debug>:zlibstaticd.lib>$<$<CONFIG:Release>:zlibstatic.lib>"  "${LIBRARY_OUTPUT_PATH}/$<CONFIG>"
)
else(WIN32)
ExternalProject_Add(hdf5
GIT_REPOSITORY https://bitbucket.hdfgroup.org/scm/hdffv/hdf5.git
GIT_TAG 1.10/master
GIT_SHALLOW ON
PREFIX ${HDF5_PREFIX}

CMAKE_ARGS 
 -DBUILD_SHARED_LIBS=OFF 
 -DBUILD_TESTING=OFF 
 -DHDF5_BUILD_EXAMPLES=OFF 
 -DHDF5_BUILD_HL_LIB=OFF 
 -DHDF5_BUILD_TOOLS=OFF 
 -DHDF5_ENABLE_EMBEDDED_LIBINFO=OFF 
 -DHDF5_ENABLE_HSIZET=OFF 
 -DHDF5_ALLOW_EXTERNAL_SUPPORT="GIT" 
 -DHDF5_ENABLE_Z_LIB_SUPPORT=ON
 -DZLIB_URL=https://github.com/madler/zlib 
 -DZLIB_BRANCH=master

INSTALL_COMMAND  ""
#	${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/libhdf5.a" "${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/libhdf5_cpp.a" "${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/libz.a"  "${EXECUTABLE_OUTPUT_PATH}"
)
endif(WIN32)

ExternalProject_Get_Property(hdf5 SOURCE_DIR BINARY_DIR)
 SET(HDF5_INCLUDE_DIRS ${SOURCE_DIR}/src ${SOURCE_DIR}/c++/src ${BINARY_DIR})
 
 message(STATUS ${SOURCE_DIR})
 message(STATUS ${BINARY_DIR})

include_directories (${HDF5_INCLUDE_DIRS})
include_directories("$ENV{HDPS_INSTALL_DIR}/$<CONFIGURATION>/include/")

set(SOURCES
    src/HDF5Loader.h
    src/HDF5Loader.cpp
	src/HDF5LoaderFactory.h
	src/HDF5LoaderFactory.cpp
    src/HDF5Loader.json

	src/DataContainerInterface.h
	src/DataContainerInterface.cpp
	src/H5Utils.h
	src/H5Utils.cpp

	src/HDF5_TOME_Loader.h
    src/HDF5_TOME_Loader.cpp

	src/HDF5_10X_Loader.h
	src/HDF5_10X_Loader.cpp

	src/HDF5_AD_Loader.h
	src/HDF5_AD_Loader.cpp
)



source_group( Plugin FILES ${SOURCES})



add_library(${PROJECT} SHARED ${SOURCES})

target_compile_definitions(${PROJECT} PRIVATE BIOVAULT_BFLOAT16_CONVERTING_CONSTRUCTORS)
target_link_libraries(${PROJECT} Qt5::Widgets)
target_link_libraries(${PROJECT} Qt5::WebEngineWidgets)
target_link_libraries(${PROJECT} "$ENV{HDPS_INSTALL_DIR}/$<CONFIGURATION>/lib/HDPS_Public.lib")
target_link_libraries(${PROJECT} "$ENV{HDPS_INSTALL_DIR}/$<CONFIGURATION>/lib/PointData.lib")

target_link_libraries(${PROJECT} ${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/$<CONFIG>/$<$<CONFIG:Debug>:zlibstaticd.lib>$<$<CONFIG:Release>:zlibstatic.lib>)

target_link_libraries(${PROJECT} ${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/$<CONFIG>/$<$<CONFIG:Debug>:libhdf5_cpp_D.lib>$<$<CONFIG:Release>:libhdf5_cpp.lib>)

target_link_libraries(${PROJECT} ${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/$<CONFIG>/$<$<CONFIG:Debug>:libhdf5_D.lib>$<$<CONFIG:Release>:libhdf5.lib>)

add_dependencies(${PROJECT} hdf5)

add_custom_command(TARGET ${PROJECT} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
	"$<TARGET_FILE:${PROJECT}>"
	"$ENV{HDPS_INSTALL_DIR}/$<CONFIGURATION>/Plugins/$<TARGET_FILE_NAME:${PROJECT}>"
)