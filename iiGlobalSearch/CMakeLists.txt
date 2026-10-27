cmake_minimum_required(VERSION 3.24)
project(iiGlobalSearch VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
find_package(iiSDKCore 0.18 CONFIG REQUIRED COMPONENTS Runtime Values)
find_package(iiJson CONFIG REQUIRED)
find_package(iiFileProviderCatalog 0.7.3 CONFIG REQUIRED)
find_package(iiFileProviderNative 0.7 CONFIG REQUIRED)
find_package(iiSocietyContainer CONFIG REQUIRED COMPONENTS FileTree)
find_package(SQLite3 3.27 REQUIRED)
find_package(iiNetwork 0.5 CONFIG REQUIRED)
find_package(LibXml2 2.9 REQUIRED)

add_library(iiGlobalSearchAnswer src/Answer.cpp)
add_library(iiGlobalSearch::Answer ALIAS iiGlobalSearchAnswer)
set_target_properties(iiGlobalSearchAnswer PROPERTIES EXPORT_NAME Answer)
target_compile_features(iiGlobalSearchAnswer PUBLIC cxx_std_23)
target_include_directories(iiGlobalSearchAnswer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/iiGlobalSearch>)
target_link_libraries(iiGlobalSearchAnswer PUBLIC iiSDKCore::Values PRIVATE iiSDKCore::Runtime)

add_library(iiGlobalSearch src/GlobalSearch.cpp src/LocalAsset.cpp)
add_library(iiGlobalSearch::iiGlobalSearch ALIAS iiGlobalSearch)
target_compile_features(iiGlobalSearch PUBLIC cxx_std_23)
target_include_directories(iiGlobalSearch PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/iiGlobalSearch>)
target_link_libraries(iiGlobalSearch PUBLIC iiGlobalSearchAnswer PRIVATE iiSDKCore::Runtime iiJson::iiJson
    iiFileProvider::Catalog iiFileProvider::Native iiSocietyContainer::FileTree SQLite3::SQLite3)

add_library(iiGlobalSearchWeb src/WebSearch.cpp src/WebHtml.cpp src/WebRenderer.cpp src/WebCli.cpp)
add_library(iiGlobalSearch::Web ALIAS iiGlobalSearchWeb)
set_target_properties(iiGlobalSearchWeb PROPERTIES EXPORT_NAME Web)
target_compile_features(iiGlobalSearchWeb PUBLIC cxx_std_23)
target_include_directories(iiGlobalSearchWeb PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/iiGlobalSearch>)
target_link_libraries(iiGlobalSearchWeb PUBLIC iiGlobalSearchAnswer PRIVATE iiSDKCore::Runtime iiJson::iiJson iiFileProvider::Native
    iiNetwork::iiNetwork LibXml2::LibXml2 CURL::libcurl)

add_library(iiGlobalSearchExample OBJECT example/search.cpp)
target_link_libraries(iiGlobalSearchExample PRIVATE iiGlobalSearch)

add_executable(ii-global-search src/main.cpp)
target_link_libraries(ii-global-search PRIVATE iiGlobalSearch iiGlobalSearchWeb iiJson::iiJson)

if(BUILD_TESTING)
    add_executable(iiGlobalSearchAnswerTests tests/Answer.cpp)
    target_link_libraries(iiGlobalSearchAnswerTests PRIVATE iiGlobalSearchAnswer)
    add_test(NAME iiGlobalSearch.answer COMMAND iiGlobalSearchAnswerTests)
    set_tests_properties(iiGlobalSearch.answer PROPERTIES TIMEOUT 30)
    add_executable(iiGlobalSearchTests tests/GlobalSearch.cpp)
    target_link_libraries(iiGlobalSearchTests PRIVATE iiGlobalSearch
        iiFileProvider::Catalog iiFileProvider::Native)
    add_executable(iiGlobalSearchBenchmark tests/benchmark.cpp)
    target_link_libraries(iiGlobalSearchBenchmark PRIVATE iiGlobalSearch iiFileProvider::Catalog)
    add_test(NAME iiGlobalSearch.index COMMAND iiGlobalSearchTests ${CMAKE_CURRENT_BINARY_DIR})
    add_executable(iiGlobalSearchWebTests tests/WebSearchUnit.cpp)
    target_link_libraries(iiGlobalSearchWebTests PRIVATE iiGlobalSearchWeb iiNetwork::iiNetwork)
    add_test(NAME iiGlobalSearch.web COMMAND iiGlobalSearchWebTests)
    set_tests_properties(iiGlobalSearch.web PROPERTIES TIMEOUT 30)
    add_executable(iiGlobalSearchWebIntegrationTests tests/WebSearch.cpp)
    target_link_libraries(iiGlobalSearchWebIntegrationTests PRIVATE iiGlobalSearchWeb iiNetwork::iiNetwork)
    add_test(NAME iiGlobalSearch.web.integration COMMAND iiGlobalSearchWebIntegrationTests ${CMAKE_CURRENT_BINARY_DIR} $<TARGET_FILE:ii-global-search>)
    set_tests_properties(iiGlobalSearch.web.integration PROPERTIES DISABLED TRUE TIMEOUT 120)
    add_test(NAME iiGlobalSearch.cli COMMAND ${CMAKE_COMMAND}
        -DCLI=$<TARGET_FILE:ii-global-search> -DRESOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/resources
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli.cmake)
    set_tests_properties(iiGlobalSearch.cli PROPERTIES DISABLED TRUE DEPENDS iiGlobalSearch.index)
endif()

install(TARGETS iiGlobalSearchAnswer EXPORT iiGlobalSearchAnswerTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(EXPORT iiGlobalSearchAnswerTargets NAMESPACE iiGlobalSearch::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiGlobalSearch)
install(TARGETS iiGlobalSearch EXPORT iiGlobalSearchTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS iiGlobalSearchWeb EXPORT iiGlobalSearchWebTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ii-global-search RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES src/Answer.h src/GlobalSearch.h src/WebSearch.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiGlobalSearch)
install(FILES resources/web-seeds.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/iiGlobalSearch)
install(EXPORT iiGlobalSearchTargets NAMESPACE iiGlobalSearch::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiGlobalSearch)
install(EXPORT iiGlobalSearchWebTargets NAMESPACE iiGlobalSearch::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiGlobalSearch)
configure_package_config_file(cmake/iiGlobalSearchConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/iiGlobalSearchConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiGlobalSearch)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/iiGlobalSearchConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/iiGlobalSearchConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/iiGlobalSearchConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiGlobalSearch)
