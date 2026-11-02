cmake_minimum_required(VERSION 3.23)
project(iiVolumetricSolidMesh VERSION 0.1.0 LANGUAGES CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

add_library(iiVolumetricSolidMesh src/VolumetricSolidMesh.cpp src/BoxCad.cpp)
add_library(iiVolumetricSolidMesh::iiVolumetricSolidMesh ALIAS iiVolumetricSolidMesh)
target_compile_features(iiVolumetricSolidMesh PUBLIC cxx_std_23)
target_include_directories(iiVolumetricSolidMesh PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/iiVolumetricSolidMesh>)
set_target_properties(iiVolumetricSolidMesh PROPERTIES CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON WINDOWS_EXPORT_ALL_SYMBOLS ON)
if(MSVC)
  target_compile_options(iiVolumetricSolidMesh PRIVATE /W4 /permissive-)
else()
  target_compile_options(iiVolumetricSolidMesh PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(BUILD_TESTING)
  add_executable(iiVolumetricSolidMeshTests tests/VolumetricSolidMeshTests.cpp)
  target_link_libraries(iiVolumetricSolidMeshTests PRIVATE iiVolumetricSolidMesh::iiVolumetricSolidMesh)
  add_test(NAME iiVolumetricSolidMeshTests COMMAND iiVolumetricSolidMeshTests)
endif()
option(IIVOLUMETRIC_BUILD_EXAMPLE "Build the API usage example" OFF)
if(IIVOLUMETRIC_BUILD_EXAMPLE)
  add_executable(iiVolumetricSolidMeshExample example/box.cpp)
  target_link_libraries(iiVolumetricSolidMeshExample PRIVATE iiVolumetricSolidMesh::iiVolumetricSolidMesh)
endif()
install(TARGETS iiVolumetricSolidMesh EXPORT iiVolumetricSolidMeshTargets)
install(FILES src/VolumetricSolidMesh.h src/BoxCad.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiVolumetricSolidMesh)
install(EXPORT iiVolumetricSolidMeshTargets NAMESPACE iiVolumetricSolidMesh:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiVolumetricSolidMesh)
configure_package_config_file(cmake/iiVolumetricSolidMeshConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/iiVolumetricSolidMeshConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiVolumetricSolidMesh)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/iiVolumetricSolidMeshConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/iiVolumetricSolidMeshConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/iiVolumetricSolidMeshConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiVolumetricSolidMesh)
