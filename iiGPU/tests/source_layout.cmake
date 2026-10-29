if(NOT SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(EXISTS "${SOURCE_ROOT}/src/iiGPU" OR EXISTS "${SOURCE_ROOT}/src/iiWebGPU")
    message(FATAL_ERROR "The source tree must not repeat package names")
endif()
file(GLOB _source_entries "${SOURCE_ROOT}/src/*")
foreach(_entry IN LISTS _source_entries)
    get_filename_component(_name "${_entry}" NAME)
    string(TOLOWER "${_name}" _folded_name)
    if(IS_DIRECTORY "${_entry}" AND _folded_name STREQUAL "webgpu")
        message(FATAL_ERROR "Source directory would shadow the third-party webgpu headers on case-insensitive filesystems")
    endif()
endforeach()
foreach(_file GPU.h GPU.cpp Runtime/Completion.h Runtime/Completion.cpp
        Backends/Backend.h Backends/Metal.mm Backends/Vulkan.cpp
        WebGPUComponent/WebGPU.h WebGPUComponent/WebGPU.cpp
        WebGPUComponent/Memory.h WebGPUComponent/Memory.cpp
        WebGPUComponent/Texture.h WebGPUComponent/Texture.cpp
        WebGPUComponent/Arena.h WebGPUComponent/Arena.cpp)
    if(NOT EXISTS "${SOURCE_ROOT}/src/${_file}")
        message(FATAL_ERROR "Missing source in its responsibility directory: ${_file}")
    endif()
endforeach()
