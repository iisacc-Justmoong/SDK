if(IS_DIRECTORY "${SOURCE_ROOT}/src/iiGlobalSearch")
    message(FATAL_ERROR "The source directory repeats the project name")
endif()
foreach(_file Answers/Answer.h Answers/Answer.cpp Local/GlobalSearch.h Local/GlobalSearch.cpp
        Local/LocalAsset.cpp Web/WebSearch.h Web/WebSearch.cpp Web/WebHtml.h Web/WebHtml.cpp
        Web/WebRenderer.h Web/WebRenderer.cpp CLI/main.cpp)
    if(NOT EXISTS "${SOURCE_ROOT}/src/${_file}")
        message(FATAL_ERROR "Missing responsibility-grouped source: ${_file}")
    endif()
endforeach()
