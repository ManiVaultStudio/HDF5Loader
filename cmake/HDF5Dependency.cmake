#set(hdf5_VERSION 1.12.1)
SET(hdf5_VERSION "1.14.2" CACHE STRING "Version of HDF5 Library")
SET_PROPERTY(CACHE hdf5_VERSION PROPERTY STRINGS 1.12.1 1.14.2)

set(USE_HDF5_ARTIFACTORY_LIBS FALSE CACHE BOOL "Use the prebuilt libraries from artifactory")
if(NOT USE_HDF5_ARTIFACTORY_LIBS)
	set(HDF5_ARTIFACTORY_LIBS_INSTALLED FALSE CACHE BOOL "The prebuild libraries from artifactory are installed")
endif()

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
	
	get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
	if(is_multi_config)	    
	   message(STATUS "Multi-Config Generator")
	   set(HDF5_LIB_PATH "${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/$<CONFIG>")
	else()
	    message(STATUS "Single-Config Generator")
	   set(HDF5_LIB_PATH "${CMAKE_CURRENT_BINARY_DIR}/${HDF5_PREFIX}/src/hdf5-build/bin/")
	endif()    
	if(MSVC)
		set(ZLIB_LIBRARIES "${HDF5_LIB_PATH}/$<$<CONFIG:Debug>:zlibstaticd.lib>$<$<CONFIG:Release>:zlibstatic.lib>")
		set(HDF5_CXX_STATIC_LIBRARY "${HDF5_LIB_PATH}/$<$<CONFIG:Debug>:libhdf5_cpp_D.lib>$<$<CONFIG:Release>:libhdf5_cpp.lib>")
		set(HDF5_C_STATIC_LIBRARY "${HDF5_LIB_PATH}/$<$<CONFIG:Debug>:libhdf5_D.lib>$<$<CONFIG:Release>:libhdf5.lib>")
	endif(MSVC)
	if(APPLE)
		set(ZLIB_LIBRARIES "${HDF5_LIB_PATH}/libz.a")
		set(HDF5_CXX_STATIC_LIBRARY "${HDF5_LIB_PATH}/$<$<CONFIG:Debug>:libhdf5_cpp_debug.a>$<$<CONFIG:Release>:libhdf5_cpp.a>")
		set(HDF5_C_STATIC_LIBRARY "${HDF5_LIB_PATH}/$<$<CONFIG:Debug>:libhdf5_debug.a>$<$<CONFIG:Release>:libhdf5.a>")			
	endif(APPLE)
	if(LINUX)
		set(ZLIB_LIBRARIES "${HDF5_LIB_PATH}/libz.a")
		set(HDF5_CXX_STATIC_LIBRARY "${HDF5_LIB_PATH}/libhdf5_cpp.a")
		set(HDF5_C_STATIC_LIBRARY "${HDF5_LIB_PATH}/libhdf5.a")
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
	BUILD_BYPRODUCTS  ${HDF5_C_STATIC_LIBRARY} ${HDF5_CXX_STATIC_LIBRARY} ${ZLIB_LIBRARIES}
	)

	ExternalProject_Get_Property(hdf5 SOURCE_DIR BINARY_DIR)
	SET(HDF5_INCLUDE_DIR ${SOURCE_DIR}/src ${SOURCE_DIR}/src/H5FDsubfiling ${SOURCE_DIR}/c++/src ${BINARY_DIR} ${BINARY_DIR}/src)
endif()

