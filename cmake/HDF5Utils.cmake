# -----------------------------------------------------------------------------
# Target include directories, linking and properties
# -----------------------------------------------------------------------------
function(SetBuildSettings PROJNAME)

	target_link_libraries(${PROJNAME} PRIVATE OpenMP::OpenMP_CXX)

	target_compile_features(${PROJNAME} PRIVATE cxx_std_20)
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
        message(STATUS "Linking with ${HDF5_CXX_STATIC_LIBRARY} and ${ZLIB_LIBRARIES}")
        target_include_directories(${PROJNAME} PRIVATE ${HDF5_INCLUDE_DIR})
        target_link_libraries(${PROJNAME} PRIVATE ${HDF5_C_STATIC_LIBRARY} ${HDF5_CXX_STATIC_LIBRARY} ${ZLIB_LIBRARIES})

    endif()

	target_link_libraries(${PROJNAME} PRIVATE Qt6::Widgets)
	target_link_libraries(${PROJNAME} PRIVATE Qt6::WebEngineWidgets)

	target_include_directories(${PROJNAME} PRIVATE "${ManiVault_INCLUDE_DIR}")
	target_link_libraries(${PROJNAME} PRIVATE ManiVault::Core)
	target_link_libraries(${PROJNAME} PRIVATE ManiVault::PointData)
	target_link_libraries(${PROJNAME} PRIVATE ManiVault::ClusterData)

	target_include_directories(${PROJNAME} PRIVATE "${COMMON_HDF5_DIR}")

	if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    	target_compile_options(${PROJNAME} PRIVATE /bigobj)
		target_link_libraries(${PROJNAME} PRIVATE shlwapi.lib)
	endif()

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

	mv_handle_plugin_config(${PROJNAME})

endfunction()

# -----------------------------------------------------------------------------
# Miscellaneous
# -----------------------------------------------------------------------------
function(SetDebugEnvironment PROJNAME)

	# Automatically set the debug environment (command + working directory) for MSVC
	if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
		set_property(TARGET ${PROJNAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<IF:$<CONFIG:DEBUG>,${ManiVault_INSTALL_DIR}/Debug,$<IF:$<CONFIG:RELWITHDEBINFO>,${ManiVault_INSTALL_DIR}/RelWithDebInfo,${ManiVault_INSTALL_DIR}/Release>>)
		set_property(TARGET ${PROJNAME} PROPERTY VS_DEBUGGER_COMMAND $<IF:$<CONFIG:DEBUG>,"${ManiVault_INSTALL_DIR}/Debug/ManiVault Studio.exe",$<IF:$<CONFIG:RELWITHDEBINFO>,"${ManiVault_INSTALL_DIR}/RelWithDebInfo/ManiVault Studio.exe","${ManiVault_INSTALL_DIR}/Release/ManiVault Studio.exe">>)
	endif()

endfunction()
