# Keep localized /showIncludes output usable by Ninja after CMake regeneration.
if(WIN32 AND MSVC)
    # Active SDKs use ordinary headers, with no CXX_MODULES file sets. Avoid
    # unnecessary dynamic module edges (and stale-object link scheduling) on
    # this Windows Ninja toolchain. MSVC /showIncludes still tracks headers.
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
    add_link_options("$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:/MANIFESTINPUT:${CMAKE_CURRENT_LIST_DIR}/windows-utf8.manifest>")
    get_filename_component(_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    if(IS_DIRECTORY "${_compiler_bin}/1042" AND NOT IS_DIRECTORY "${_compiler_bin}/1033")
        set(CMAKE_CL_SHOWINCLUDES_PREFIX "참고: 포함 파일: ")
        set(CMAKE_C_CL_SHOWINCLUDES_PREFIX "참고: 포함 파일: ")
        set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "참고: 포함 파일: ")
    endif()
endif()
