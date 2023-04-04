cmake_minimum_required(VERSION 3.17.0)

set(PROJECT "HDF5Loader")

PROJECT(${PROJECT})

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

# -----------------------------------------------------------------------------
# Library versions
# -----------------------------------------------------------------------------
set(hdf5_VERSION 1.12.1)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /DWIN32 /EHsc /MP /bigobj /permissive- /Zc:__cplusplus")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:LIBCMT")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD")
	set(CMAKE_CXX_FLAGS_RELEASE_INIT "/GL")
	set(CMAKE_SHARED_LINKER_FLAGS_INIT "/LTCG:PGOptimize")
	
endif(MSVC)

if (APPLE)
	# set(BREW_PATH "brew" CACHE FILEPATH "the path to the brew that matches the target build architecture (m1 or x86_64" )
	# execute_process(COMMAND ${BREW_PATH} --prefix libomp RESULT_VARIABLE LIBOMP_ROOT)
	set(OpenMP_ROOT "/usr/local/homebrew/opt/libomp")
endif ()

# Normalize the incoming install path
file(TO_CMAKE_PATH $ENV{HDPS_INSTALL_DIR} INSTALL_DIR)
find_package(Qt6 COMPONENTS Widgets WebEngineWidgets REQUIRED)

set(USE_HDF5_ARTIFACTORY_LIBS FALSE CACHE BOOL "Use the prebuilt libraries from artifactory")
if(NOT USE_HDF5_ARTIFACTORY_LIBS)
	set(HDF5_ARTIFACTORY_LIBS_INSTALLED FALSE CACHE BOOL "The prebuild libraries from artifactory are installed")
endif()

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
	src/DataTransform.h
	src/DataTransform.cpp

	src/HDF5_TOME_Loader.h
	src/HDF5_TOME_Loader.cpp

	src/HDF5_10X_Loader.h
	src/HDF5_10X_Loader.cpp

	src/HDF5_AD_Loader.h
	src/HDF5_AD_Loader.cpp
)

source_group( Plugin FILES ${SOURCES})
add_library(${PROJECT} SHARED ${SOURCES})

include(InstallArtifactoryPackage)
set(LIBRARY_INSTALL_DIR ${PROJECT_BINARY_DIR})
if (USE_HDF5_ARTIFACTORY_LIBS)
	if (NOT HDF5_ARTIFACTORY_LIBS_INSTALLED) 
		message(STATUS "Installing artifactory packages to: ${LIBRARY_INSTALL_DIR}")
		# Both HDILib and flann are available prebuilt in the lkeb-artifactory as combined Debug/Release packages
		# lz4 is also available in the lkb-artifactory in separate Debug and |Release packages
		install_artifactory_package(PACKAGE_NAME hdf5 PACKAGE_VERSION ${hdf5_VERSION} PACKAGE_BUILDER lkeb COMBINED_PACKAGE TRUE) 

		message(STATUS "module path ${CMAKE_MODULE_PATH}")
		message(STATUS "Call find_package hdf5 search ${LIBRARY_INSTALL_DIR}/hdf5/share/cmake/hdf5")
		find_package(HDF5 COMPONENTS CXX static PATHS "${LIBRARY_INSTALL_DIR}/hdf5/share/cmake/hdf5" CONFIG REQUIRED)
		message(STATUS "Include for HDF5 at ${HDF5_INCLUDE_DIR} - version ${HDF5_VERSION_STRING}")
		set(ARTIFACTORY_LIBS_INSTALLED TRUE CACHE BOOL "Use the prebuilt libraries from artifactory" FORCE)
	endif()
else()
	# Het HDF5 with ZLIB
	message(STATUS "hdf5 as external project")
	include(ExternalProject)

	set(HDF5_PREFIX hdf5)
	message(STATUS " HDF5_VERSION: ${hdf5_VERSION}")
	string(REPLACE "." "_" hdf5_UNDERSCORE_VERSION ${hdf5_VERSION})
	message(STATUS " GitHub Tag: ${hdf5_UNDERSCORE_VERSION}")
	set(HDF5_GIT_TAG  "hdf5-${hdf5_UNDERSCORE_VERSION}")
	set(git_stash_save_options --all --quiet)
	if(MSVC)
	set(HDF5_INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/$<CONFIG>/$<$<CONFIG:Debug>:libhdf5_D.lib>$<$<CONFIG:Release>:libhdf5.lib>" "${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/$<CONFIG>/$<$<CONFIG:Debug>:libhdf5_cpp_D.lib>$<$<CONFIG:Release>:libhdf5_cpp.lib>" "${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/$<CONFIG>/$<$<CONFIG:Debug>:zlibstaticd.lib>$<$<CONFIG:Release>:zlibstatic.lib>"  "${EXECUTABLE_OUTPUT_PATH}/$<CONFIG>")
	endif(MSVC)
	if(APPLE)
	set(HDF5_INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/$<CONFIG>/$<$<CONFIG:Debug>:libhdf5_debug.a>$<$<CONFIG:Release>:libhdf5.a>" "${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/$<CONFIG>/$<$<CONFIG:Debug>:libhdf5_cpp_debug.a>$<$<CONFIG:Release>:libhdf5_cpp.a>" "${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/$<CONFIG>/libz.a"  "${EXECUTABLE_OUTPUT_PATH}/$<CONFIG>")
	endif(APPLE)
	if(LINUX)
	set(HDF5_INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/libhdf5.a" "${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/libhdf5_cpp.a" "${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/libz.a"  "${EXECUTABLE_OUTPUT_PATH}")
	endif(LINUX)

	ExternalProject_Add(hdf5
	GIT_REPOSITORY https://github.com/HDFGroup/hdf5.git
	GIT_TAG ${HDF5_GIT_TAG}
	GIT_SHALLOW ON
	UPDATE_COMMAND ""
	PREFIX ${HDF5_PREFIX}
	CMAKE_ARGS
			-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
			-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
	-DBUILD_SHARED_LIBS=OFF
	-DHDF5_BUILD_CPP_LIB=ON
	-DBUILD_TESTING=OFF 
	-DHDF5_BUILD_EXAMPLES=OFF 
	-DHDF5_BUILD_HL_LIB=OFF 
	-DHDF5_BUILD_TOOLS=OFF 
	-DHDF5_BUILD_UTILS=OFF
	-DHDF5_ENABLE_EMBEDDED_LIBINFO=OFF 
	-DHDF5_ENABLE_HSIZET=OFF 
	-DHDF5_ALLOW_EXTERNAL_SUPPORT="GIT" 
	-DHDF5_ENABLE_Z_LIB_SUPPORT=ON
	-DZLIB_URL=https://github.com/madler/zlib 
	-DZLIB_BRANCH=master

	INSTALL_COMMAND ""
	)

	ExternalProject_Get_Property(hdf5 SOURCE_DIR BINARY_DIR)
	SET(HDF5_INCLUDE_DIRS ${SOURCE_DIR}/src ${SOURCE_DIR}/c++/src ${BINARY_DIR} ${BINARY_DIR}/src)

	message(STATUS ${SOURCE_DIR})
	message(STATUS ${BINARY_DIR})

	target_include_directories(${PROJECT} PRIVATE ${HDF5_INCLUDE_DIRS})
	target_include_directories(${PROJECT} PRIVATE "${INSTALL_DIR}/$<CONFIG>/include/")
endif()

find_package(OpenMP REQUIRED)
if(OPENMP_FOUND)
	message (STATUS "OpenMP found ${OpenMP_CXX_LIB_NAMES}")
	target_include_directories(${PROJECT} PRIVATE ${OpenMP_CXX_INCLUDE_DIRS})
	set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
	set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}") 
	if (APPLE)
		target_link_libraries (${PROJECT} ${OpenMP_CXX_LIBRARIES})
	endif()
else()
    message(WARNING, "OpenMP not found!")    
endif()
message(STATUS ${SOURCE_DIR})
message(STATUS ${BINARY_DIR})
target_include_directories(${PROJECT} PRIVATE "${INSTALL_DIR}/$<CONFIGURATION>/include/")

target_compile_features(${PROJECT} PRIVATE cxx_std_17)

if(MSVC)
    set(LIB_LINK_SUFFIX ".lib") 
else()
    set(LIB_LINK_SUFFIX "${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif(MSVC)
set(HDPS_LINK_LIBRARY "${INSTALL_DIR}/$<CONFIG>/lib/${CMAKE_SHARED_LIBRARY_PREFIX}HDPS_Public${LIB_LINK_SUFFIX}")
set(POINTDATA_LINK_LIBRARY "${INSTALL_DIR}/$<CONFIG>/lib/${CMAKE_SHARED_LIBRARY_PREFIX}PointData${LIB_LINK_SUFFIX}")
set(CLUSTERDATA_LINK_LIBRARY "${INSTALL_DIR}/$<CONFIG>/lib/${CMAKE_SHARED_LIBRARY_PREFIX}ClusterData${LIB_LINK_SUFFIX}")

if (NOT USE_HDF5_ARTIFACTORY_LIBS)
	add_dependencies(${PROJECT} hdf5)
else()
	target_include_directories("${PROJECT}" PRIVATE "${HDF5_INCLUDE_DIR}")
	target_include_directories("${PROJECT}" PRIVATE "${LIBRARY_INSTALL_DIR}/zlib/$<CONFIG>/include")
	target_include_directories("${PROJECT}" PRIVATE "${INSTALL_DIR}/$<CONFIG>/include/")
endif()

target_compile_definitions(${PROJECT} PRIVATE BIOVAULT_BFLOAT16_CONVERTING_CONSTRUCTORS)
target_link_libraries(${PROJECT} Qt6::Widgets)
target_link_libraries(${PROJECT} Qt6::WebEngineWidgets)
target_link_libraries(${PROJECT} "${HDPS_LINK_LIBRARY}")
target_link_libraries(${PROJECT} "${POINTDATA_LINK_LIBRARY}")
target_link_libraries(${PROJECT} "${CLUSTERDATA_LINK_LIBRARY}")

if (NOT USE_HDF5_ARTIFACTORY_LIBS)
	if(MSVC)
		target_link_libraries(${PROJECT} ${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/$<CONFIG>/$<$<CONFIG:Debug>:zlibstaticd.lib>$<$<CONFIG:Release>:zlibstatic.lib>)
		target_link_libraries(${PROJECT} ${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/$<CONFIG>/$<$<CONFIG:Debug>:libhdf5_cpp_D.lib>$<$<CONFIG:Release>:libhdf5_cpp.lib>)
		target_link_libraries(${PROJECT} ${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/$<CONFIG>/$<$<CONFIG:Debug>:libhdf5_D.lib>$<$<CONFIG:Release>:libhdf5.lib>)
	else()
		target_link_libraries(${PROJECT} ${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/libz${CMAKE_STATIC_LIBRARY_SUFFIX})
		target_link_libraries(${PROJECT} ${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/libhdf5_cpp${CMAKE_STATIC_LIBRARY_SUFFIX})
		target_link_libraries(${PROJECT} ${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/libhdf5${CMAKE_STATIC_LIBRARY_SUFFIX})
	endif()
else()
	find_package (HDF5 NAMES hdf5 REQUIRED COMPONENTS CXX static PATH "${CMAKE_CURRENT_SOURCE_DIR}/hdf5" REQUIRED)
	set(ZLIB_ROOT ${HDF5_ZLIB_ROOT})
	find_package (ZLIB REQUIRED)
	target_link_libraries (${PROJECT}  ${HDF5_CXX_STATIC_LIBRARY} ${ZLIB_LIBRARIES})
endif()

install(TARGETS ${PROJECT}
   RUNTIME DESTINATION Plugins COMPONENT SHAREDLIB
)

add_custom_command(TARGET ${PROJECT} POST_BUILD
	COMMAND "${CMAKE_COMMAND}"
	--install ${CMAKE_CURRENT_BINARY_DIR}
	--config $<CONFIGURATION>
	--prefix ${INSTALL_DIR}/$<CONFIGURATION>
)

# Automatically set the debug environment (command + working directory) for MSVC in debug mode
set_property(TARGET ${PROJECT} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<IF:$<CONFIG:DEBUG>,${INSTALL_DIR}/debug,${INSTALL_DIR}/release>)
set_property(TARGET ${PROJECT} PROPERTY VS_DEBUGGER_COMMAND $<IF:$<CONFIG:DEBUG>,${INSTALL_DIR}/debug/HDPS.exe,${INSTALL_DIR}/release/HDPS.exe>)
