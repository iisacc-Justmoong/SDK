cmake_minimum_required(VERSION 3.24)
project(iiJob VERSION 0.1.0 LANGUAGES CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
find_package(SQLite3 3.26 REQUIRED)
if(NOT TARGET SQLite3::SQLite3)
    add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
endif()

add_library(iiJob STATIC src/Job.cpp src/Job.h)
add_library(iiJob::iiJob ALIAS iiJob)
target_compile_features(iiJob PUBLIC cxx_std_23)
set_target_properties(iiJob PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
target_include_directories(iiJob PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/iiJob>)
target_link_libraries(iiJob PRIVATE SQLite3::SQLite3)

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(iiJobTests tests/job.cpp)
    target_link_libraries(iiJobTests PRIVATE iiJob::iiJob)
    add_test(NAME iiJob.persistence COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_contract.py" "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        $<TARGET_FILE:iiJobTests>)
    set_tests_properties(iiJob.persistence PROPERTIES TIMEOUT 300)
endif()

install(TARGETS iiJob EXPORT iiJobTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES src/Job.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiJob)
install(EXPORT iiJobTargets NAMESPACE iiJob:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiJob)
configure_package_config_file(cmake/iiJobConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/iiJobConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiJob)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/iiJobConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiJobConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/iiJobConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiJob)
