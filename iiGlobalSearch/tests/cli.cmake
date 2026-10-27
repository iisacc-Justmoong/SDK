execute_process(COMMAND "${CLI}" index "${RESOURCE_ROOT}/tests/Library.societycontainer" maple
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT output MATCHES "portable.txt" OR NOT output MATCHES "completed")
    message(FATAL_ERROR "CLI search failed: ${result} ${output} ${error}")
endif()
execute_process(COMMAND "${CLI}" index "${RESOURCE_ROOT}/tests"
    RESULT_VARIABLE invalid OUTPUT_QUIET ERROR_QUIET)
if(invalid EQUAL 0)
    message(FATAL_ERROR "CLI accepted a directory outside a .societycontainer")
endif()
