cmake_minimum_required(VERSION 3.24)
project(iiLocalizer VERSION 0.1.0 LANGUAGES CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
option(IILOCALIZER_BUILD_TESTS "Build iiLocalizer contract tests" ${PROJECT_IS_TOP_LEVEL})

add_library(iiLocalizer STATIC src/iiLocalizer.cpp src/iiLocalizer.h src/iiLocalizer)
add_library(iiLocalizer::iiLocalizer ALIAS iiLocalizer)
target_compile_features(iiLocalizer PUBLIC cxx_std_23)
target_include_directories(iiLocalizer PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
set_target_properties(iiLocalizer PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)

add_library(iiLocalizerC SHARED src/iiLocalizerC.cpp)
add_library(iiLocalizer::C ALIAS iiLocalizerC)
target_link_libraries(iiLocalizerC PRIVATE iiLocalizer)
set_target_properties(iiLocalizerC PROPERTIES EXPORT_NAME C WINDOWS_EXPORT_ALL_SYMBOLS ON CXX_EXTENSIONS OFF)

if(BUILD_TESTING AND IILOCALIZER_BUILD_TESTS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(iiLocalizerTests tests/localizer_tests.cpp)
    target_link_libraries(iiLocalizerTests PRIVATE iiLocalizer::iiLocalizer)
    target_compile_definitions(iiLocalizerTests PRIVATE "IILOCALIZER_TEST_PO_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/resources/tests/ko.po\"")
    add_test(NAME iiLocalizer.Native COMMAND iiLocalizerTests)
    add_test(NAME iiLocalizer.Python COMMAND "${CMAKE_COMMAND}" -E env
        "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/src/python"
        "IILOCALIZER_LIBRARY=$<TARGET_FILE:iiLocalizerC>"
        "IILOCALIZER_TEST_PO=${CMAKE_CURRENT_SOURCE_DIR}/resources/tests/ko.po"
        "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_api.py")
    find_program(IILOCALIZER_MSGFMT_EXECUTABLE msgfmt)
    if(IILOCALIZER_MSGFMT_EXECUTABLE)
        execute_process(COMMAND "${IILOCALIZER_MSGFMT_EXECUTABLE}" --version
            RESULT_VARIABLE _iilocalizer_msgfmt_status OUTPUT_VARIABLE _iilocalizer_msgfmt_version
            ERROR_QUIET)
    endif()
    if(IILOCALIZER_MSGFMT_EXECUTABLE AND _iilocalizer_msgfmt_status EQUAL 0
       AND _iilocalizer_msgfmt_version MATCHES "GNU gettext")
        add_test(NAME iiLocalizer.GettextCompatibility COMMAND "${CMAKE_COMMAND}" -E env
            "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/src/python"
            "IILOCALIZER_LIBRARY=$<TARGET_FILE:iiLocalizerC>"
            "IILOCALIZER_TEST_PO=${CMAKE_CURRENT_SOURCE_DIR}/resources/tests/ko.po"
            "IILOCALIZER_MSGFMT=${IILOCALIZER_MSGFMT_EXECUTABLE}"
            "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/gettext_interop.py")
    endif()
endif()

install(TARGETS iiLocalizer iiLocalizerC EXPORT iiLocalizerTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES src/iiLocalizer src/iiLocalizer.h src/iiLocalizerC.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES src/python/iilocalizer.py DESTINATION ${CMAKE_INSTALL_LIBDIR}/python)
install(EXPORT iiLocalizerTargets NAMESPACE iiLocalizer:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiLocalizer)
configure_package_config_file(cmake/iiLocalizerConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/iiLocalizerConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiLocalizer)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/iiLocalizerConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiLocalizerConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/iiLocalizerConfigVersion.cmake"
    cmake/iiLocalizerMacros.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiLocalizer)
