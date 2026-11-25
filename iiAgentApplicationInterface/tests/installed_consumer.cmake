# Stage the built package and compile an independent consumer without SDK search paths.
set(stage "${IIAGENTAPPLICATIONINTERFACE_BUILD}/package-test")
set(consumer "${IIAGENTAPPLICATIONINTERFACE_BUILD}/consumer-test")
set(config_args)
set(ctest_args)
if(NOT "${CONFIGURATION}" STREQUAL "")
    list(APPEND config_args --config "${CONFIGURATION}")
    list(APPEND ctest_args -C "${CONFIGURATION}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${IIAGENTAPPLICATIONINTERFACE_BUILD}" --prefix "${stage}" ${config_args}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Package staging failed: ${output}\n${error}")
endif()
# The renamed package must not install the earlier local prototype's public entry points.
foreach(old_path
        "${stage}/${INSTALL_LIBDIR}/cmake/iiAgent"
        "${stage}/include/iisdk/iiAgent"
        "${stage}/${INSTALL_DATADIR}/iiAgent/python/iiagent.py")
    if(EXISTS "${old_path}")
        message(FATAL_ERROR "Unexpected old package entry point: ${old_path}")
    endif()
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=CPATH --unset=CPLUS_INCLUDE_PATH
    --unset=LIBRARY_PATH --unset=DYLD_LIBRARY_PATH "${CMAKE_COMMAND}"
    -S "${IIAGENTAPPLICATIONINTERFACE_SOURCE}/tests/consumer" -B "${consumer}"
    "-DiiAgentApplicationInterface_DIR=${stage}/${INSTALL_LIBDIR}/cmake/iiAgentApplicationInterface"
    -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Installed package configuration failed: ${output}\n${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${consumer}" ${config_args}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Installed consumer build failed: ${output}\n${error}")
endif()
execute_process(COMMAND "${CTEST_PROGRAM}" --test-dir "${consumer}" --output-on-failure ${ctest_args}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Installed consumer test failed: ${output}\n${error}")
endif()
if(VERIFY_PYTHON)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=DYLD_LIBRARY_PATH
        "IIAGENTAPPLICATIONINTERFACE_LIBRARY=${stage}/${INSTALL_RUNTIME_LIBDIR}/${LIBRARY_NAME}"
        "PYTHONPATH=${stage}/${INSTALL_DATADIR}/iiAgentApplicationInterface/python" "PYTHONDONTWRITEBYTECODE=1"
        "${PYTHON_EXECUTABLE}" "${IIAGENTAPPLICATIONINTERFACE_SOURCE}/tests/python_contracts.py"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Installed Python test failed: ${output}\n${error}")
    endif()
endif()
