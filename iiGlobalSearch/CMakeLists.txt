cmake_minimum_required(VERSION 3.24)
project(iiGlobalSearch VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
option(BUILD_SHARED_LIBS "Build SDK libraries as shared libraries" ON)
if(EMSCRIPTEN AND BUILD_SHARED_LIBS)
    set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)
endif()
option(IIGLOBALSEARCH_BUILD_EXAMPLES "Build examples" OFF)
option(IIGLOBALSEARCH_BUILD_BENCHMARKS "Build benchmarks" OFF)
include(cmake/SourceHeaders.cmake)
find_package(iiSDKCore 0.18 CONFIG REQUIRED COMPONENTS Runtime Values)
find_package(iiJson CONFIG REQUIRED)
find_package(iiFileProviderCatalog 0.7.3 CONFIG REQUIRED)
if(NOT iiFileProviderCatalog_HAS_FILE_SEARCH_INDEX OR NOT iiFileProviderCatalog_HAS_FILE_SEARCH_PAGE OR NOT iiFileProviderCatalog_HAS_PACKAGES)
    message(FATAL_ERROR "iiGlobalSearch requires iiFileProviderCatalog with paged FileSearchIndex and package support")
endif()
find_package(iiFileProviderNative 0.7 CONFIG REQUIRED)
find_package(iiSocietyContainer CONFIG REQUIRED COMPONENTS FileTree)
find_package(iiNetwork 0.5 CONFIG REQUIRED)
find_package(LibXml2 2.9 REQUIRED)

add_library(iiGlobalSearchAnswer src/Answers/Answer.cpp)
add_library(iiGlobalSearch::Answer ALIAS iiGlobalSearchAnswer)
set_target_properties(iiGlobalSearchAnswer PROPERTIES EXPORT_NAME Answer)
target_compile_features(iiGlobalSearchAnswer PUBLIC cxx_std_23)
target_include_directories(iiGlobalSearchAnswer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src> $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/iiGlobalSearch>)
target_link_libraries(iiGlobalSearchAnswer PUBLIC iiSDKCore::Values PRIVATE iiSDKCore::Runtime)

add_library(iiGlobalSearch src/Local/GlobalSearch.cpp src/Local/LocalAsset.cpp src/Local/JsonValue.cpp)
add_library(iiGlobalSearch::iiGlobalSearch ALIAS iiGlobalSearch)
target_compile_features(iiGlobalSearch PUBLIC cxx_std_23)
target_include_directories(iiGlobalSearch PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src> $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/iiGlobalSearch>)
target_link_libraries(iiGlobalSearch PUBLIC iiGlobalSearchAnswer PRIVATE iiSDKCore::Runtime iiJson::iiJson
    iiFileProvider::Catalog iiFileProvider::Native iiSocietyContainer::FileTree)

add_library(iiGlobalSearchWeb src/Web/WebSearch.cpp src/Web/WebHtml.cpp src/Web/WebRenderer.cpp src/Web/DirectoryEvents.cpp src/Web/WebCli.cpp src/Local/JsonValue.cpp)
add_library(iiGlobalSearch::Web ALIAS iiGlobalSearchWeb)
set_target_properties(iiGlobalSearchWeb PROPERTIES EXPORT_NAME Web)
target_compile_features(iiGlobalSearchWeb PUBLIC cxx_std_23)
target_include_directories(iiGlobalSearchWeb PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src> $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/iiGlobalSearch>)
target_link_libraries(iiGlobalSearchWeb PUBLIC iiGlobalSearchAnswer PRIVATE iiSDKCore::Runtime iiJson::iiJson iiFileProvider::Native
    iiNetwork::iiNetwork LibXml2::LibXml2 CURL::libcurl)
foreach(_target IN ITEMS iiGlobalSearchAnswer iiGlobalSearch iiGlobalSearchWeb)
    set_target_properties(${_target} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    if(BUILD_SHARED_LIBS)
        if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
            set_target_properties(${_target} PROPERTIES FRAMEWORK TRUE FRAMEWORK_VERSION A
                MACOSX_FRAMEWORK_IDENTIFIER "com.iisacc.${_target}")
        elseif(EMSCRIPTEN)
            target_compile_options(${_target} PRIVATE -fPIC -sSIDE_MODULE=1)
            target_link_options(${_target} PRIVATE -sSIDE_MODULE=1)
        elseif(APPLE)
            set_property(TARGET ${_target} APPEND PROPERTY INSTALL_RPATH "@loader_path")
        elseif(UNIX)
            set_property(TARGET ${_target} APPEND PROPERTY INSTALL_RPATH "$ORIGIN")
        endif()
    endif()
endforeach()

if(IIGLOBALSEARCH_BUILD_EXAMPLES)
    add_library(iiGlobalSearchExample OBJECT example/search.cpp)
    target_link_libraries(iiGlobalSearchExample PRIVATE iiGlobalSearch)
endif()

add_executable(ii-global-search src/CLI/main.cpp)
target_link_libraries(ii-global-search PRIVATE iiGlobalSearch iiGlobalSearchWeb iiJson::iiJson)

if(IIGLOBALSEARCH_BUILD_BENCHMARKS)
    add_executable(iiGlobalSearchBenchmark tests/benchmark.cpp)
    target_link_libraries(iiGlobalSearchBenchmark PRIVATE iiGlobalSearch iiFileProvider::Catalog)
endif()

if(BUILD_TESTING)
    add_executable(iiGlobalSearchJsonValueTests tests/JsonValue.cpp src/Local/JsonValue.cpp)
    target_compile_features(iiGlobalSearchJsonValueTests PRIVATE cxx_std_23)
    target_link_libraries(iiGlobalSearchJsonValueTests PRIVATE iiSDKCore::Runtime iiJson::iiJson)
    add_test(NAME iiGlobalSearch.json_value COMMAND iiGlobalSearchJsonValueTests)
    if(CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        add_test(NAME iiGlobalSearch.build_targets COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DEXAMPLES=$<IF:$<BOOL:${IIGLOBALSEARCH_BUILD_EXAMPLES}>,ON,OFF>
            -DBENCHMARKS=$<IF:$<BOOL:${IIGLOBALSEARCH_BUILD_BENCHMARKS}>,ON,OFF>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/build_targets.cmake)
    endif()
    add_test(NAME iiGlobalSearch.source_layout COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/source_layout.cmake)
    add_executable(iiGlobalSearchAnswerTests tests/Answer.cpp)
    target_link_libraries(iiGlobalSearchAnswerTests PRIVATE iiGlobalSearchAnswer)
    add_test(NAME iiGlobalSearch.answer COMMAND iiGlobalSearchAnswerTests)
    set_tests_properties(iiGlobalSearch.answer PROPERTIES TIMEOUT 30)
    add_executable(iiGlobalSearchTests tests/GlobalSearch.cpp)
    target_link_libraries(iiGlobalSearchTests PRIVATE iiGlobalSearch
        iiFileProvider::Catalog iiFileProvider::Native)
    add_test(NAME iiGlobalSearch.index COMMAND iiGlobalSearchTests ${CMAKE_CURRENT_SOURCE_DIR}/resources)
    add_executable(iiGlobalSearchSessionTests tests/ObjectSession.cpp)
    target_link_libraries(iiGlobalSearchSessionTests PRIVATE iiGlobalSearch)
    add_test(NAME iiGlobalSearch.session COMMAND iiGlobalSearchSessionTests)
    set_tests_properties(iiGlobalSearch.session PROPERTIES TIMEOUT 30)
    add_executable(iiGlobalSearchWebTests tests/WebSearchUnit.cpp)
    target_link_libraries(iiGlobalSearchWebTests PRIVATE iiGlobalSearchWeb iiNetwork::iiNetwork)
    add_test(NAME iiGlobalSearch.web COMMAND iiGlobalSearchWebTests)
    if(NOT CMAKE_CROSSCOMPILING AND (NOT DEFINED BUILD_SHARED_LIBS OR BUILD_SHARED_LIBS))
        foreach(_pair IN ITEMS "iiGlobalSearchAnswer;iiGlobalSearchAnswerTests" "iiGlobalSearch;iiGlobalSearchTests" "iiGlobalSearchWeb;iiGlobalSearchWebTests")
            list(GET _pair 0 _library)
            list(GET _pair 1 _image)
            add_test(NAME ${_library}.SharedLink COMMAND ${CMAKE_COMMAND}
                "-DLIBRARY_TYPE=$<TARGET_PROPERTY:${_library},TYPE>" "-DLIBRARY=$<TARGET_FILE:${_library}>"
                "-DIMAGE=$<TARGET_FILE:${_image}>" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/shared_link.cmake")
        endforeach()
    endif()
    set_tests_properties(iiGlobalSearch.web PROPERTIES TIMEOUT 30)
    add_executable(iiGlobalSearchDirectoryEventsTests tests/DirectoryEvents.cpp)
    target_link_libraries(iiGlobalSearchDirectoryEventsTests PRIVATE iiGlobalSearchWeb iiFileProvider::Native)
    add_test(NAME iiGlobalSearch.directory_events COMMAND iiGlobalSearchDirectoryEventsTests ${CMAKE_CURRENT_SOURCE_DIR}/resources/tests)
    set_tests_properties(iiGlobalSearch.directory_events PROPERTIES TIMEOUT 30)
    add_executable(iiGlobalSearchWebNetworkTests tests/WebNetwork.cpp)
    target_link_libraries(iiGlobalSearchWebNetworkTests PRIVATE iiGlobalSearchWeb iiNetwork::iiNetwork)
    add_test(NAME iiGlobalSearch.web_network COMMAND iiGlobalSearchWebNetworkTests)
    set_tests_properties(iiGlobalSearch.web_network PROPERTIES TIMEOUT 30)
    add_executable(iiGlobalSearchProcessCompletionTests tests/ProcessCompletion.cpp)
    target_link_libraries(iiGlobalSearchProcessCompletionTests PRIVATE iiSDKCore::Runtime)
    add_test(NAME iiGlobalSearch.process_completion COMMAND iiGlobalSearchProcessCompletionTests)
    set_tests_properties(iiGlobalSearch.process_completion PROPERTIES TIMEOUT 30)
    add_executable(iiGlobalSearchWebIntegrationTests tests/WebSearch.cpp)
    target_link_libraries(iiGlobalSearchWebIntegrationTests PRIVATE iiGlobalSearchWeb iiNetwork::iiNetwork)
    add_test(NAME iiGlobalSearch.web.integration COMMAND iiGlobalSearchWebIntegrationTests ${CMAKE_CURRENT_SOURCE_DIR}/resources $<TARGET_FILE:ii-global-search>)
    set_tests_properties(iiGlobalSearch.web.integration PROPERTIES DISABLED TRUE TIMEOUT 120)
    add_test(NAME iiGlobalSearch.cli COMMAND ${CMAKE_COMMAND}
        -DCLI=$<TARGET_FILE:ii-global-search> -DRESOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/resources
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli.cmake)
    set_tests_properties(iiGlobalSearch.cli PROPERTIES DISABLED TRUE DEPENDS iiGlobalSearch.index)
endif()

install(TARGETS iiGlobalSearchAnswer EXPORT iiGlobalSearchAnswerTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} FRAMEWORK DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(EXPORT iiGlobalSearchAnswerTargets NAMESPACE iiGlobalSearch::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiGlobalSearch)
install(TARGETS iiGlobalSearch EXPORT iiGlobalSearchTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} FRAMEWORK DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS iiGlobalSearchWeb EXPORT iiGlobalSearchWebTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} FRAMEWORK DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ii-global-search RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES src/Answers/Answer.h src/Local/GlobalSearch.h src/Web/WebSearch.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiGlobalSearch)
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
