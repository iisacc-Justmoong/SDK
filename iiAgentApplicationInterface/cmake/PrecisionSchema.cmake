# Exact decimal validation is a native, isolated C-ABI dependency. Cargo is a
# build-time tool only; the resulting target library has no network/file resolver.
find_program(IIAGENTAPPLICATIONINTERFACE_CARGO_EXECUTABLE NAMES cargo REQUIRED)
set(IIAGENTAPPLICATIONINTERFACE_RUST_TARGET "" CACHE STRING "Rust target triple for the target platform (required when cross-compiling)")
set(IIAGENTAPPLICATIONINTERFACE_PRECISION_PROFILE "release" CACHE STRING "Rust backend build profile: release or debug")
set_property(CACHE IIAGENTAPPLICATIONINTERFACE_PRECISION_PROFILE PROPERTY STRINGS release debug)
set(IIAGENTAPPLICATIONINTERFACE_PRECISION_JOBS "2" CACHE STRING "Maximum Cargo compile jobs")
set(IIAGENTAPPLICATIONINTERFACE_PRECISION_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/precision-cargo" CACHE PATH "Cargo output directory inside the module build tree")
cmake_path(ABSOLUTE_PATH IIAGENTAPPLICATIONINTERFACE_PRECISION_TARGET_DIR NORMALIZE OUTPUT_VARIABLE _iiagent_cargo_dir)
set(_iiagent_module_build_root "${CMAKE_CURRENT_SOURCE_DIR}/build")
cmake_path(IS_PREFIX _iiagent_module_build_root "${_iiagent_cargo_dir}" NORMALIZE _iiagent_cargo_inside_build)
if(NOT _iiagent_cargo_inside_build)
    message(FATAL_ERROR "The precision backend target directory must be inside the module's build/ tree")
endif()
if(NOT IIAGENTAPPLICATIONINTERFACE_PRECISION_PROFILE MATCHES "^(release|debug)$")
    message(FATAL_ERROR "The precision backend profile must be release or debug")
endif()
if(NOT IIAGENTAPPLICATIONINTERFACE_PRECISION_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "IIAGENTAPPLICATIONINTERFACE_PRECISION_JOBS must be a positive integer")
endif()
if(CMAKE_CROSSCOMPILING AND NOT IIAGENTAPPLICATIONINTERFACE_RUST_TARGET)
    message(FATAL_ERROR "Set IIAGENTAPPLICATIONINTERFACE_RUST_TARGET to the target's Rust triple")
endif()
set(_iiagent_cargo_args build --locked --lib --jobs "${IIAGENTAPPLICATIONINTERFACE_PRECISION_JOBS}"
    --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/src/PrecisionSchema/Cargo.toml")
if(IIAGENTAPPLICATIONINTERFACE_PRECISION_PROFILE STREQUAL "release")
    list(APPEND _iiagent_cargo_args --release)
endif()
if(IIAGENTAPPLICATIONINTERFACE_RUST_TARGET)
    list(APPEND _iiagent_cargo_args --target "${IIAGENTAPPLICATIONINTERFACE_RUST_TARGET}")
    set(_iiagent_cargo_output "${_iiagent_cargo_dir}/${IIAGENTAPPLICATIONINTERFACE_RUST_TARGET}/${IIAGENTAPPLICATIONINTERFACE_PRECISION_PROFILE}")
else()
    set(_iiagent_cargo_output "${_iiagent_cargo_dir}/${IIAGENTAPPLICATIONINTERFACE_PRECISION_PROFILE}")
endif()
if(WIN32)
    set(_iiagent_precision_runtime "${_iiagent_cargo_output}/iiagent_precision_schema.dll")
    set(_iiagent_precision_import "${_iiagent_cargo_output}/iiagent_precision_schema.dll.lib")
elseif(APPLE)
    set(_iiagent_precision_runtime "${_iiagent_cargo_output}/libiiagent_precision_schema.dylib")
else()
    set(_iiagent_precision_runtime "${_iiagent_cargo_output}/libiiagent_precision_schema.so")
endif()
if(APPLE)
    # Rust emits an absolute build-tree install name by default. Use an rpath
    # identity so a staged or moved SDK never points back into the build tree.
    set(_iiagent_precision_fix_id COMMAND "${CMAKE_INSTALL_NAME_TOOL}" -id
        "@rpath/libiiagent_precision_schema.dylib" "${_iiagent_precision_runtime}")
endif()
add_custom_command(OUTPUT "${_iiagent_precision_runtime}"
    COMMAND "${CMAKE_COMMAND}" -E env --unset=MAKEFLAGS --unset=MFLAGS "CARGO_TARGET_DIR=${_iiagent_cargo_dir}"
        "${IIAGENTAPPLICATIONINTERFACE_CARGO_EXECUTABLE}" ${_iiagent_cargo_args}
    ${_iiagent_precision_fix_id}
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/PrecisionSchema/Cargo.toml"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/PrecisionSchema/Cargo.lock"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/PrecisionSchema/src/lib.rs"
    VERBATIM)
add_custom_target(iiagent_precision_build DEPENDS "${_iiagent_precision_runtime}")
add_library(iiagent_precision_backend SHARED IMPORTED GLOBAL)
add_dependencies(iiagent_precision_backend iiagent_precision_build)
set_target_properties(iiagent_precision_backend PROPERTIES IMPORTED_LOCATION "${_iiagent_precision_runtime}")
if(WIN32)
    set_target_properties(iiagent_precision_backend PROPERTIES IMPORTED_IMPLIB "${_iiagent_precision_import}")
    install(PROGRAMS "${_iiagent_precision_runtime}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES "${_iiagent_precision_import}" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
else()
    install(PROGRAMS "${_iiagent_precision_runtime}" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()
