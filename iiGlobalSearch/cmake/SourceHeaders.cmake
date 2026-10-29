# The flat build aliases preserve the existing installed header API.
foreach(_area IN ITEMS Answers Local Web)
    file(GLOB _headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/${_area}/*.h")
    foreach(_header IN LISTS _headers)
        get_filename_component(_name "${_header}" NAME)
        if(DEFINED _search_header_${_name})
            message(FATAL_ERROR "Duplicate iiGlobalSearch header basename: ${_name}")
        endif()
        set("_search_header_${_name}" "${_header}")
        file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/include/${_name}"
            CONTENT "#pragma once\n#include \"${_header}\"\n")
    endforeach()
endforeach()
