if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

if(IS_DIRECTORY "${PROJECT_SOURCE_DIR}/src/iiCompression")
    message(FATAL_ERROR "The source tree must not repeat the project name")
endif()

foreach(_file Gzip.h Gzip.cpp)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/src/${_file}")
        message(FATAL_ERROR "The gzip interface and implementation must be adjacent under src/: ${_file}")
    endif()
endforeach()
