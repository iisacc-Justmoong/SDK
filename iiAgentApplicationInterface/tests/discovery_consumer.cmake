# Stage the module, then build and test a separate CMake application with automatic discovery.
set(stage "${MODULE_BUILD}/discovery-stage")
set(consumer "${MODULE_BUILD}/discovery-consumer")
set(config_args)
set(ctest_args)
if(NOT "${CONFIGURATION}" STREQUAL "")
    list(APPEND config_args --config "${CONFIGURATION}")
    list(APPEND ctest_args -C "${CONFIGURATION}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${MODULE_BUILD}" --prefix "${stage}" ${config_args}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Staging failed: ${output}\n${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=CPATH --unset=CPLUS_INCLUDE_PATH
    --unset=LIBRARY_PATH --unset=DYLD_LIBRARY_PATH "${CMAKE_COMMAND}"
    -S "${MODULE_SOURCE}/tests/discovery_consumer" -B "${consumer}"
    "-DiiAgentApplicationInterface_DIR=${stage}/${INSTALL_LIBDIR}/cmake/iiAgentApplicationInterface"
    "-DTEST_RESOURCES=${MODULE_SOURCE}/resources/build_discovery"
    -DTEST_DISCOVERY_EXTRA=OFF
    -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Discovery consumer configuration failed: ${output}\n${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${consumer}" ${config_args}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Discovery consumer build failed: ${output}\n${error}")
endif()
# Reconfigure with a new compiler definition and verify the generated catalog changes.
set(catalog "${consumer}/iiAgentApplicationInterface/discovery_app/BuildCatalog.cpp")
file(READ "${catalog}" initial_catalog)
string(FIND "${initial_catalog}" "demo::Project::conditional_member" initial_position)
if(NOT initial_position EQUAL -1)
    message(FATAL_ERROR "Disabled conditional member was unexpectedly discovered")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${MODULE_SOURCE}/tests/discovery_consumer" -B "${consumer}"
    -DTEST_DISCOVERY_EXTRA=ON RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Discovery consumer reconfiguration failed: ${output}\n${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${consumer}" ${config_args}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Discovery consumer rebuild failed: ${output}\n${error}")
endif()
file(READ "${catalog}" updated_catalog)
string(FIND "${updated_catalog}" "demo::Project::conditional_member" updated_position)
if(updated_position EQUAL -1)
    message(FATAL_ERROR "Conditional member missing after build configuration changed")
endif()
execute_process(COMMAND "${CTEST_PROGRAM}" --test-dir "${consumer}" --output-on-failure ${ctest_args}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Discovery consumer test failed: ${output}\n${error}")
endif()

# Multi-config databases contain several flag sets; reject them until configuration isolation exists.
find_program(NINJA_EXECUTABLE ninja)
if(NINJA_EXECUTABLE)
    execute_process(COMMAND "${CMAKE_COMMAND}" -G "Ninja Multi-Config"
        -S "${MODULE_SOURCE}/tests/discovery_consumer" -B "${MODULE_BUILD}/discovery-multiconfig"
        "-DiiAgentApplicationInterface_DIR=${stage}/${INSTALL_LIBDIR}/cmake/iiAgentApplicationInterface"
        "-DTEST_RESOURCES=${MODULE_SOURCE}/resources/build_discovery"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(result EQUAL 0 OR NOT "${error}" MATCHES "single-configuration generator")
        message(FATAL_ERROR "Multi-config discovery must report unsupported: ${output}\n${error}")
    endif()
endif()
