cmake_minimum_required(VERSION 3.18.0)
message(STATUS "Building with cmake version ${CMAKE_VERSION}")

set(PROJECT "HDF5Loader")

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

#set(hdf5_VERSION 1.12.1)
SET(hdf5_VERSION "1.14.2" CACHE STRING "Version of HDF5 Library")
SET_PROPERTY(CACHE hdf5_VERSION PROPERTY STRINGS 1.12.1 1.14.2)

if(MSVC)
	set(CMAKE_GENERATOR_TOOLSET "host=x64" CACHE STRING "Platform Toolset" FORCE)

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /DWIN32 /EHsc /MP /bigobj /permissive- /Zc:__cplusplus")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:LIBCMT")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")
	
	set(CMAKE_CXX_FLAGS_RELEASE_INIT "/GL")
	set(CMAKE_SHARED_LINKER_FLAGS_INIT "/LTCG:PGOptimize")
	
	cmake_host_system_information(RESULT TOTAL_PHYSICAL_MEMORY QUERY TOTAL_PHYSICAL_MEMORY)
	#message(STATUS "Available Memory = "${TOTAL_PHYSICAL_MEMORY} )
	if(${TOTAL_PHYSICAL_MEMORY} LESS 16385)
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD /Ob0")
		message(STATUS "Disabling Inline Function Expansion")
	else()
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD")
	endif()
endif(MSVC)

if (APPLE)
	# set(BREW_PATH "brew" CACHE FILEPATH "the path to the brew that matches the target build architecture (m1 or x86_64" )
	# execute_process(COMMAND ${BREW_PATH} --prefix libomp RESULT_VARIABLE LIBOMP_ROOT)
	set(OpenMP_ROOT "/usr/local/homebrew/opt/libomp")
endif ()

# Check if the directory to the ManiVault installation has been provided
if(NOT DEFINED MV_INSTALL_DIR)
    set(MV_INSTALL_DIR "" CACHE PATH "Directory where ManiVault is installed")
    message(FATAL_ERROR "Please set MV_INSTALL_DIR to the directory where ManiVault is installed")
endif()
file(TO_CMAKE_PATH ${MV_INSTALL_DIR} MV_INSTALL_DIR)

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

	src/H5ADUtils.h
	src/H5ADUtils.cpp
)

set(PLUGIN_MOC_HEADERS
	src/DataTransform.h
    src/HDF5LoaderFactory.h
)

source_group( Plugin FILES ${SOURCES})
add_library(${PROJECT} SHARED ${SOURCES})

qt_wrap_cpp(HDF5LOADER_MOC ${PLUGIN_MOC_HEADERS} TARGET ${PROJECT})
target_sources(${PROJECT} PRIVATE ${HDF5LOADER_MOC})

include(InstallArtifactoryPackage)
set(LIBRARY_INSTALL_DIR ${PROJECT_BINARY_DIR})
if (USE_HDF5_ARTIFACTORY_LIBS)
	if (NOT HDF5_ARTIFACTORY_LIBS_INSTALLED) 
		message(STATUS "Installing artifactory packages to: ${LIBRARY_INSTALL_DIR}")
		# Both HDILib and flann are available prebuilt in the lkeb-artifactory as combined Debug/Release packages
		# lz4 is also available in the lkb-artifactory in separate Debug and |Release packages
		install_artifactory_package(PACKAGE_NAME hdf5 PACKAGE_VERSION ${hdf5_VERSION} PACKAGE_BUILDER lkeb COMBINED_PACKAGE TRUE) 

		message(STATUS "HDF5 root path ${hdf5_ROOT}")
	endif()
	set(HDF5_ROOT ${LIBRARY_INSTALL_DIR}/${package_name})
	set(HDF5_USE_STATIC_LIBRARIES TRUE)
	find_package(HDF5 COMPONENTS CXX C static REQUIRED NO_MODULE)
	#get_cmake_property(_variableNames VARIABLES)
	#list (SORT _variableNames)
	#foreach (_variableName ${_variableNames})
	#		message(STATUS "${_variableName}=${${_variableName}}")
	#endforeach()
	message(STATUS "Include for HDF5 at ${HDF5_INCLUDE_DIR} - version ${HDF5_VERSION_STRING}")
	set(ARTIFACTORY_LIBS_INSTALLED TRUE CACHE BOOL "Use the prebuilt libraries from artifactory" FORCE)
	target_include_directories(${PROJECT} PRIVATE ${HDF5_INCLUDE_DIR})
	message(status "hdf5 include ${HDF5_INCLUDE_DIR}")

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
	SET(ALL_INCLUDE_DIRS ${SOURCE_DIR}/src ${SOURCE_DIR}/src/H5FDsubfiling ${SOURCE_DIR}/c++/src ${BINARY_DIR} ${BINARY_DIR}/src)
	target_include_directories(${PROJECT} PRIVATE ${ALL_INCLUDE_DIRS})
endif()

target_include_directories(${PROJECT} PRIVATE "${MV_INSTALL_DIR}/$<CONFIG>/include/")

find_package(OpenMP REQUIRED)
if(OpenMP_FOUND)
    message(STATUS "OpenMP found")
    target_link_libraries(${PROJECT} OpenMP::OpenMP_CXX)
    target_compile_options(${PROJECT} PRIVATE ${OpenMP_CXX_FLAGS})
else()
    message(WARNING "OpenMP not found!")
endif()

message(STATUS ${SOURCE_DIR})
message(STATUS ${BINARY_DIR})
target_include_directories(${PROJECT} PRIVATE "${MV_INSTALL_DIR}/$<CONFIG>/include/")

target_compile_features(${PROJECT} PRIVATE cxx_std_17)
set_target_properties(${PROJECT} PROPERTIES CXX_STANDARD 17)

set(MV_LINK_PATH "${MV_INSTALL_DIR}/$<CONFIG>/lib")
set(PLUGIN_LINK_PATH "${MV_INSTALL_DIR}/$<CONFIG>/$<IF:$<CXX_COMPILER_ID:MSVC>,lib,Plugins>")
set(MV_LINK_SUFFIX $<IF:$<CXX_COMPILER_ID:MSVC>,${CMAKE_LINK_LIBRARY_SUFFIX},${CMAKE_SHARED_LIBRARY_SUFFIX}>)

set(MV_LINK_LIBRARY "${MV_LINK_PATH}/${CMAKE_SHARED_LIBRARY_PREFIX}MV_Public${MV_LINK_SUFFIX}")
set(POINTDATA_LINK_LIBRARY "${PLUGIN_LINK_PATH}/${CMAKE_SHARED_LIBRARY_PREFIX}PointData${MV_LINK_SUFFIX}") 
set(CLUSTERDATA_LINK_LIBRARY "${PLUGIN_LINK_PATH}/${CMAKE_SHARED_LIBRARY_PREFIX}ClusterData${MV_LINK_SUFFIX}") 

if (NOT USE_HDF5_ARTIFACTORY_LIBS)
	add_dependencies(${PROJECT} hdf5)
else()
	target_include_directories("${PROJECT}" PRIVATE "${HDF5_INCLUDE_DIR}")
	target_include_directories("${PROJECT}" PRIVATE "${LIBRARY_INSTALL_DIR}/zlib/$<CONFIG>/include")
	target_include_directories("${PROJECT}" PRIVATE "${MV_INSTALL_DIR}/$<CONFIG>/include/")
endif()

target_compile_definitions(${PROJECT} PRIVATE BIOVAULT_BFLOAT16_CONVERTING_CONSTRUCTORS)
target_link_libraries(${PROJECT} Qt6::Widgets)
target_link_libraries(${PROJECT} Qt6::WebEngineWidgets)
target_link_libraries(${PROJECT} "${MV_LINK_LIBRARY}")
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
	message(status "find_package searching zlib at ${HDF5_ZLIB_ROOT}")
	set(ZLIB_ROOT ${HDF5_ZLIB_ROOT})
	find_package (ZLIB REQUIRED)
	message(STATUS "Linking with ${HDF5_CXX_LIBRARIES} ${HDF5_LIBRARIES} and ${ZLIB_LIBRARIES}")
	target_link_libraries (${PROJECT} ${HDF5_CXX_STATIC_LIBRARY} ${ZLIB_LIBRARIES})
endif()
if(MSVC)
	target_link_libraries(${PROJECT} shlwapi.lib)
endif(MSVC)

install(TARGETS ${PROJECT}
    RUNTIME DESTINATION Plugins COMPONENT PLUGINS # Windows .dll
    LIBRARY DESTINATION Plugins COMPONENT PLUGINS # Linux/Mac .so
)

add_custom_command(TARGET ${PROJECT} POST_BUILD
	COMMAND "${CMAKE_COMMAND}"
	--install ${CMAKE_CURRENT_BINARY_DIR}
	--config $<CONFIG>
	--prefix ${MV_INSTALL_DIR}/$<CONFIG>
)

# Automatically set the debug environment (command + working directory) for MSVC
if(MSVC)
    set_property(TARGET ${PROJECT} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<IF:$<CONFIG:DEBUG>,${MV_INSTALL_DIR}/debug,${MV_INSTALL_DIR}/release>)
    set_property(TARGET ${PROJECT} PROPERTY VS_DEBUGGER_COMMAND $<IF:$<CONFIG:DEBUG>,"${MV_INSTALL_DIR}/debug/ManiVault Studio.exe","${MV_INSTALL_DIR}/release/ManiVault Studio.exe">)
endif()
