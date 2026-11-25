# Build a separate application that explicitly composes the independent SDK modules.
set(stage "${MODULE_BUILD}/external-package")
set(consumer "${MODULE_BUILD}/external-host")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${MODULE_BUILD}" --prefix "${stage}" --config "${CONFIGURATION}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Module staging failed: ${output}\n${error}")
endif()
set(prefix "${stage};${SDK_ROOT}/iiNetwork;${SDK_ROOT}/iiSDKCore;${SDK_ROOT}/iiJson;${OPENSSL_ROOT}")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${MODULE_SOURCE}/tests/external_host" -B "${consumer}"
    "-DMODULE_SOURCE=${MODULE_SOURCE}" "-DCMAKE_PREFIX_PATH=${prefix}" "-DOPENSSL_ROOT_DIR=${OPENSSL_ROOT}"
    "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}" "-DCMAKE_BUILD_TYPE=${CONFIGURATION}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "External host configuration failed: ${output}\n${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${consumer}" --config "${CONFIGURATION}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "External host build failed: ${output}\n${error}")
endif()
execute_process(COMMAND "${PYTHON_EXECUTABLE}" -B "${MODULE_SOURCE}/tests/external_process.py"
    "${consumer}/iiAgentExternalHost" "${stage}/${INSTALL_DATADIR}/iiAgentApplicationInterface/python"
    "${CXX_COMPILER}" "${MODULE_LIBRARY}" "${MODULE_SOURCE}" "${MODULE_BUILD}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "External agent integration failed: ${output}\n${error}")
endif()
message(STATUS "${output}")
