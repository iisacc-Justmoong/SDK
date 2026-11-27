cmake_minimum_required(VERSION 3.24)
project(iiAgentApplicationInterface VERSION 0.1.0 LANGUAGES C CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
find_package(Threads REQUIRED)
find_package(iiSDKCore 0.20 CONFIG REQUIRED COMPONENTS Contracts Runtime HINTS "$ENV{HOME}/.local/SDK/iiSDKCore")
include(cmake/CorvusSchema.cmake)
include(cmake/PrecisionSchema.cmake)
option(BUILD_SHARED_LIBS "Build a shared library" ON)
option(IIAGENTAPPLICATIONINTERFACE_BUILD_PYTHON "Install and test Python bindings" ON)
option(IIAGENTAPPLICATIONINTERFACE_BUILD_EXAMPLES "Build host integration examples" ON)
option(IIAGENTAPPLICATIONINTERFACE_TEST_IISDK_INTEGRATION "Test a separately composed iiNetwork host" OFF)
set(IIAGENTAPPLICATIONINTERFACE_SDK_ROOT "$ENV{HOME}/.local/SDK" CACHE PATH "Root of the host application's installed SDK modules")
set(IIAGENTAPPLICATIONINTERFACE_OPENSSL_ROOT "" CACHE PATH "Optional OpenSSL prefix used by the external iiNetwork host")
if(IIAGENTAPPLICATIONINTERFACE_BUILD_PYTHON AND NOT BUILD_SHARED_LIBS)
    message(FATAL_ERROR "The Python binding requires BUILD_SHARED_LIBS=ON")
endif()
# Generated build-only forwarding headers preserve the installed, namespaced includes.
foreach(_header Agent.h AgentC.h BuildCatalog.h SourceProcedure.h External.h Command.h)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/public/iiAgentApplicationInterface/${_header}"
        CONTENT "#pragma once\n#include \"${CMAKE_CURRENT_SOURCE_DIR}/src/${_header}\"\n")
endforeach()
add_library(iiAgentApplicationInterface src/Agent.cpp src/AgentC.cpp src/External.cpp src/Command.cpp src/LegacyMetas.cpp)
add_library(iiAgentApplicationInterface::iiAgentApplicationInterface ALIAS iiAgentApplicationInterface)
target_link_libraries(iiAgentApplicationInterface PUBLIC Threads::Threads)
target_link_libraries(iiAgentApplicationInterface PRIVATE iiSDKCore::Contracts iiSDKCore::Runtime
    corvus_json_schema::corvus_json_schema_static iiagent_precision_backend)
target_include_directories(iiAgentApplicationInterface SYSTEM PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/jsoncons/include" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/colorm")
target_compile_features(iiAgentApplicationInterface PUBLIC cxx_std_23)
target_include_directories(iiAgentApplicationInterface PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/public>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/ModuleImports>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/iisdk>)
target_compile_definitions(iiAgentApplicationInterface PUBLIC IISDK_IMPORT_IIAGENTAPPLICATIONINTERFACE=1)
set_target_properties(iiAgentApplicationInterface PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON WINDOWS_EXPORT_ALL_SYMBOLS ON
    BUILD_RPATH "${_iiagent_cargo_output}")
# Resolve iiSDKCore when the module is installed beside other iiSDK prefixes,
# while retaining the configured dependency path for an independent staging
# prefix. Consumers can still provide their own deployment loader paths.
if(APPLE)
    set_target_properties(iiAgentApplicationInterface PROPERTIES
        INSTALL_RPATH "@loader_path;@loader_path/../../iiSDKCore/${CMAKE_INSTALL_LIBDIR}"
        INSTALL_RPATH_USE_LINK_PATH TRUE)
elseif(UNIX)
    set_target_properties(iiAgentApplicationInterface PROPERTIES
        INSTALL_RPATH "$ORIGIN;$ORIGIN/../../iiSDKCore/${CMAKE_INSTALL_LIBDIR}"
        INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()
if(IIAGENTAPPLICATIONINTERFACE_BUILD_PYTHON)
    find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)
    Python3_add_library(iiagent_application_commands MODULE WITH_SOABI src/CommandPython.cpp)
    target_compile_features(iiagent_application_commands PRIVATE cxx_std_23)
    target_include_directories(iiagent_application_commands SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pybind11/include")
    target_link_libraries(iiagent_application_commands PRIVATE iiAgentApplicationInterface::iiAgentApplicationInterface)
    file(RELATIVE_PATH _iiagent_python_to_lib
        "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/iiAgentApplicationInterface/python"
        "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
    if(APPLE)
        set_target_properties(iiagent_application_commands PROPERTIES
            INSTALL_RPATH "@loader_path/${_iiagent_python_to_lib};@loader_path/${_iiagent_python_to_lib}/../../iiSDKCore/${CMAKE_INSTALL_LIBDIR}"
            INSTALL_RPATH_USE_LINK_PATH TRUE)
    elseif(UNIX)
        set_target_properties(iiagent_application_commands PROPERTIES
            INSTALL_RPATH "$ORIGIN/${_iiagent_python_to_lib};$ORIGIN/${_iiagent_python_to_lib}/../../iiSDKCore/${CMAKE_INSTALL_LIBDIR}"
            INSTALL_RPATH_USE_LINK_PATH TRUE)
    endif()
    install(TARGETS iiagent_application_commands
        LIBRARY DESTINATION "${CMAKE_INSTALL_DATADIR}/iiAgentApplicationInterface/python"
        RUNTIME DESTINATION "${CMAKE_INSTALL_DATADIR}/iiAgentApplicationInterface/python")
    install(FILES third_party/pybind11/LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/iiAgentApplicationInterface/licenses/pybind11")
endif()
if(BUILD_TESTING)
    add_executable(iiAgentApplicationInterfaceCommandContracts tests/command_contracts.cpp)
    add_executable(iiAgentApplicationInterfaceCommandWorker tests/command_worker.cpp)
    target_compile_features(iiAgentApplicationInterfaceCommandWorker PRIVATE cxx_std_23)
    target_link_libraries(iiAgentApplicationInterfaceCommandContracts PRIVATE iiAgentApplicationInterface::iiAgentApplicationInterface)
    add_test(NAME iiAgentApplicationInterface.CommandContracts COMMAND iiAgentApplicationInterfaceCommandContracts $<TARGET_FILE:iiAgentApplicationInterfaceCommandWorker> "${CMAKE_CURRENT_SOURCE_DIR}/resources")
    add_executable(iiAgentApplicationInterfaceContracts tests/contracts.cpp)
    target_link_libraries(iiAgentApplicationInterfaceContracts PRIVATE iiAgentApplicationInterface::iiAgentApplicationInterface)
    add_test(NAME iiAgentApplicationInterface.Contracts COMMAND iiAgentApplicationInterfaceContracts)
    add_executable(iiAgentApplicationInterfaceNativeProcedure tests/native_procedure_contracts.cpp)
    target_link_libraries(iiAgentApplicationInterfaceNativeProcedure PRIVATE iiAgentApplicationInterface::iiAgentApplicationInterface)
    add_test(NAME iiAgentApplicationInterface.NativeProcedure COMMAND iiAgentApplicationInterfaceNativeProcedure)
    add_executable(iiAgentApplicationInterfaceLifetimeContracts tests/lifetime_contracts.cpp)
    target_link_libraries(iiAgentApplicationInterfaceLifetimeContracts PRIVATE iiAgentApplicationInterface::iiAgentApplicationInterface)
    add_test(NAME iiAgentApplicationInterface.Lifetime COMMAND iiAgentApplicationInterfaceLifetimeContracts)
    add_executable(iiAgentApplicationInterfaceExternalGateway tests/external_gateway.cpp)
    target_link_libraries(iiAgentApplicationInterfaceExternalGateway PRIVATE iiAgentApplicationInterface::iiAgentApplicationInterface)
    add_test(NAME iiAgentApplicationInterface.ExternalGateway COMMAND iiAgentApplicationInterfaceExternalGateway)
    add_executable(iiAgentApplicationInterfaceProcedureAuthoring tests/procedure_authoring.cpp)
    target_link_libraries(iiAgentApplicationInterfaceProcedureAuthoring PRIVATE iiAgentApplicationInterface::iiAgentApplicationInterface)
    add_test(NAME iiAgentApplicationInterface.ProcedureAuthoring COMMAND iiAgentApplicationInterfaceProcedureAuthoring)
    add_executable(iiAgentApplicationInterfaceSourceOrchestration tests/source_orchestration.cpp)
    target_link_libraries(iiAgentApplicationInterfaceSourceOrchestration PRIVATE iiAgentApplicationInterface::iiAgentApplicationInterface)
    add_test(NAME iiAgentApplicationInterface.SourceOrchestration COMMAND iiAgentApplicationInterfaceSourceOrchestration)
    if(UNIX AND NOT CMAKE_CROSSCOMPILING)
        add_executable(iiAgentApplicationInterfaceSourceRuntimeNative tests/source_runtime_native.cpp)
        target_include_directories(iiAgentApplicationInterfaceSourceRuntimeNative PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources/native_procedure")
        target_link_libraries(iiAgentApplicationInterfaceSourceRuntimeNative PRIVATE iiAgentApplicationInterface::iiAgentApplicationInterface ${CMAKE_DL_LIBS})
        add_test(NAME iiAgentApplicationInterface.SourceRuntimeNative COMMAND iiAgentApplicationInterfaceSourceRuntimeNative
            "${CMAKE_CXX_COMPILER}" "${CMAKE_CURRENT_SOURCE_DIR}/resources/native_procedure/agent_procedure.cpp"
            "${CMAKE_CURRENT_BINARY_DIR}/runtime-compiled-procedure.so" "$<TARGET_FILE:iiAgentApplicationInterface>"
            "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/resources/native_procedure/agent_body.txt")
    endif()
    if(NOT CMAKE_CROSSCOMPILING)
        add_library(iiAgentApplicationInterfaceProcedureFixture MODULE resources/native_procedure/agent_procedure.cpp)
        target_link_libraries(iiAgentApplicationInterfaceProcedureFixture PRIVATE iiAgentApplicationInterface::iiAgentApplicationInterface)
        set_target_properties(iiAgentApplicationInterfaceProcedureFixture PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
        add_executable(iiAgentApplicationInterfaceProcedurePlugin tests/native_procedure_plugin.cpp)
        target_include_directories(iiAgentApplicationInterfaceProcedurePlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources/native_procedure")
        target_link_libraries(iiAgentApplicationInterfaceProcedurePlugin PRIVATE iiAgentApplicationInterface::iiAgentApplicationInterface ${CMAKE_DL_LIBS})
        add_test(NAME iiAgentApplicationInterface.ProcedurePlugin COMMAND iiAgentApplicationInterfaceProcedurePlugin
            $<TARGET_FILE:iiAgentApplicationInterfaceProcedureFixture>)
    endif()
    add_executable(iiAgentApplicationInterfaceCContracts tests/c_contracts.c)
    target_link_libraries(iiAgentApplicationInterfaceCContracts PRIVATE iiAgentApplicationInterface::iiAgentApplicationInterface)
    add_test(NAME iiAgentApplicationInterface.CContracts COMMAND iiAgentApplicationInterfaceCContracts)
endif()
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME iiAgentApplicationInterface.Documentation COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/src/sync_docs.py" --check)
    add_test(NAME iiAgentApplicationInterface.LegacyMetaResources COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_legacy_metas.py" --check)
    if(NOT CMAKE_CROSSCOMPILING)
        if(WIN32)
            set(_iiagent_application_interface_runtime_libdir "${CMAKE_INSTALL_BINDIR}")
        else()
            set(_iiagent_application_interface_runtime_libdir "${CMAKE_INSTALL_LIBDIR}")
        endif()
        add_test(NAME iiAgentApplicationInterface.InstalledConsumer COMMAND "${CMAKE_COMMAND}"
            "-DIIAGENTAPPLICATIONINTERFACE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DIIAGENTAPPLICATIONINTERFACE_BUILD=${CMAKE_CURRENT_BINARY_DIR}"
            "-DINSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}"
            "-DINSTALL_RUNTIME_LIBDIR=${_iiagent_application_interface_runtime_libdir}"
            "-DINSTALL_DATADIR=${CMAKE_INSTALL_DATADIR}"
            "-DLIBRARY_NAME=$<TARGET_FILE_NAME:iiAgentApplicationInterface>"
            "-DCONFIGURATION=$<CONFIG>"
            "-DCTEST_PROGRAM=${CMAKE_CTEST_COMMAND}"
            "-DVERIFY_PYTHON=${IIAGENTAPPLICATIONINTERFACE_BUILD_PYTHON}"
            "-DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/installed_consumer.cmake")
    endif()
endif()
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_test(NAME iiAgentApplicationInterface.BuildDiscoveryConsumer COMMAND "${CMAKE_COMMAND}"
        "-DMODULE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DMODULE_BUILD=${CMAKE_CURRENT_BINARY_DIR}"
        "-DINSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}"
        "-DCONFIGURATION=$<CONFIG>"
        "-DCTEST_PROGRAM=${CMAKE_CTEST_COMMAND}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/discovery_consumer.cmake")
endif()
if(BUILD_TESTING AND IIAGENTAPPLICATIONINTERFACE_BUILD_PYTHON AND IIAGENTAPPLICATIONINTERFACE_TEST_IISDK_INTEGRATION AND UNIX AND NOT CMAKE_CROSSCOMPILING)
    add_test(NAME iiAgentApplicationInterface.ExternalProcess COMMAND "${CMAKE_COMMAND}"
        "-DMODULE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}" "-DMODULE_BUILD=${CMAKE_CURRENT_BINARY_DIR}"
        "-DMODULE_LIBRARY=$<TARGET_FILE:iiAgentApplicationInterface>" "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DSDK_ROOT=${IIAGENTAPPLICATIONINTERFACE_SDK_ROOT}" "-DOPENSSL_ROOT=${IIAGENTAPPLICATIONINTERFACE_OPENSSL_ROOT}"
        "-DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}" "-DINSTALL_DATADIR=${CMAKE_INSTALL_DATADIR}"
        "-DCONFIGURATION=$<CONFIG>" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/external_consumer.cmake")
    set_tests_properties(iiAgentApplicationInterface.ExternalProcess PROPERTIES TIMEOUT 180)
endif()
if(BUILD_TESTING AND IIAGENTAPPLICATIONINTERFACE_BUILD_PYTHON)
    add_test(NAME iiAgentApplicationInterface.CommandPython COMMAND ${CMAKE_COMMAND} -E env
        "PYTHONPATH=$<TARGET_FILE_DIR:iiagent_application_commands>"
        "PYTHONDONTWRITEBYTECODE=1" "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/command_python.py")
    add_test(NAME iiAgentApplicationInterface.McpCommandServer COMMAND ${CMAKE_COMMAND} -E env
        "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/src/python" "IIAGENT_COMMAND_MODULE_DIR=$<TARGET_FILE_DIR:iiagent_application_commands>"
        "PYTHONDONTWRITEBYTECODE=1" "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/mcp_command_server.py")
    add_test(NAME iiAgentApplicationInterface.SchemaConformance COMMAND ${CMAKE_COMMAND} -E env
        "PYTHONPATH=$<TARGET_FILE_DIR:iiagent_application_commands>" "PYTHONDONTWRITEBYTECODE=1"
        "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/schema_conformance.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/json-schema-test-suite")
    set_tests_properties(iiAgentApplicationInterface.SchemaConformance PROPERTIES TIMEOUT 300)
    add_test(NAME iiAgentApplicationInterface.SchemaPrecisionConformance COMMAND ${CMAKE_COMMAND} -E env
        "PYTHONPATH=$<TARGET_FILE_DIR:iiagent_application_commands>" "PYTHONDONTWRITEBYTECODE=1"
        "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/schema_precision_conformance.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/json-schema-test-suite")
    set_tests_properties(iiAgentApplicationInterface.SchemaPrecisionConformance PROPERTIES TIMEOUT 300)
    add_test(NAME iiAgentApplicationInterface.SchemaOptionalConformance COMMAND ${CMAKE_COMMAND} -E env
        "PYTHONPATH=$<TARGET_FILE_DIR:iiagent_application_commands>" "PYTHONDONTWRITEBYTECODE=1"
        "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/schema_optional_conformance.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/json-schema-test-suite")
    set_tests_properties(iiAgentApplicationInterface.SchemaOptionalConformance PROPERTIES TIMEOUT 300)
    add_test(NAME iiAgentApplicationInterface.LegacyDraftConformance COMMAND ${CMAKE_COMMAND} -E env
        "PYTHONPATH=$<TARGET_FILE_DIR:iiagent_application_commands>" "PYTHONDONTWRITEBYTECODE=1"
        "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/legacy_draft_conformance.py")
    if(UNIX)
        add_test(NAME iiAgentApplicationInterface.CommandGateway COMMAND ${CMAKE_COMMAND} -E env
            "IIAGENT_COMMAND_MODULE_DIR=$<TARGET_FILE_DIR:iiagent_application_commands>"
            "PYTHONDONTWRITEBYTECODE=1" "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/command_gateway.py")
        set_tests_properties(iiAgentApplicationInterface.CommandGateway PROPERTIES TIMEOUT 60)
    endif()
    add_test(NAME iiAgentApplicationInterface.BuildDiscovery
        COMMAND ${CMAKE_COMMAND} -E env
        "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/src/python"
        "PYTHONDONTWRITEBYTECODE=1"
        "IIAGENTAPPLICATIONINTERFACE_LIBRARY=$<TARGET_FILE:iiAgentApplicationInterface>"
        "IIAGENTAPPLICATIONINTERFACE_TEST_BUILD=${CMAKE_CURRENT_BINARY_DIR}"
        "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/discovery_contracts.py")
    add_test(NAME iiAgentApplicationInterface.SourceRuntime
        COMMAND ${CMAKE_COMMAND} -E env
        "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/src/python"
        "PYTHONDONTWRITEBYTECODE=1"
        "IIAGENTAPPLICATIONINTERFACE_LIBRARY=$<TARGET_FILE:iiAgentApplicationInterface>"
        "IIAGENTAPPLICATIONINTERFACE_TEST_BUILD=${CMAKE_CURRENT_BINARY_DIR}"
        "IIAGENTAPPLICATIONINTERFACE_TEST_CXX=${CMAKE_CXX_COMPILER}"
        "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/source_runtime.py")
    add_test(NAME iiAgentApplicationInterface.Python
        COMMAND ${CMAKE_COMMAND} -E env
        "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/src/python"
        "PYTHONDONTWRITEBYTECODE=1"
        "IIAGENTAPPLICATIONINTERFACE_LIBRARY=$<TARGET_FILE:iiAgentApplicationInterface>"
        "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_contracts.py")
endif()

if(IIAGENTAPPLICATIONINTERFACE_BUILD_EXAMPLES)
    add_executable(iiAgentApplicationInterfaceHostExample example/host.cpp)
    target_link_libraries(iiAgentApplicationInterfaceHostExample PRIVATE iiAgentApplicationInterface::iiAgentApplicationInterface)
    add_executable(iiAgentApplicationInterfaceProcedureExample example/native_procedure.cpp)
    target_link_libraries(iiAgentApplicationInterfaceProcedureExample PRIVATE iiAgentApplicationInterface::iiAgentApplicationInterface)
endif()

install(TARGETS iiAgentApplicationInterface EXPORT iiAgentApplicationInterfaceTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES src/Agent.h src/AgentC.h src/BuildCatalog.h src/SourceProcedure.h src/External.h src/Command.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiAgentApplicationInterface)
install(FILES third_party/jsoncons/LICENSE third_party/jsoncons/A5HASH-LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/iiAgentApplicationInterface/licenses/jsoncons)
install(FILES third_party/colorm/UNLICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/iiAgentApplicationInterface/licenses/colorm)
install(FILES src/PrecisionSchema/THIRD-PARTY-NOTICES.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/iiAgentApplicationInterface/licenses/precision-schema)
install(FILES third_party/corvus/LICENSE third_party/corvus/THIRD-PARTY-NOTICES.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/iiAgentApplicationInterface/licenses/corvus)
install(FILES resources/standard-metas/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/iiAgentApplicationInterface/licenses/json-schema RENAME LICENSE)
if(NOT BUILD_SHARED_LIBS)
    get_target_property(_iiagent_corvus_static corvus_json_schema::corvus_json_schema_static IMPORTED_LOCATION)
    install(FILES "${_iiagent_corvus_static}" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(FILES "${_iiagent_corvus_root}/include/corvus_json_schema.h" "${_iiagent_corvus_root}/include/corvus_json_schema.hpp"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(DIRECTORY "${_iiagent_corvus_root}/lib/cmake/corvus_json_schema"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake")
endif()
install(FILES src/ModuleImports/iiAgentApplicationInterface DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iisdk)
install(EXPORT iiAgentApplicationInterfaceTargets NAMESPACE iiAgentApplicationInterface:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiAgentApplicationInterface)
configure_package_config_file(cmake/iiAgentApplicationInterfaceConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/iiAgentApplicationInterfaceConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiAgentApplicationInterface
    PATH_VARS CMAKE_INSTALL_DATADIR)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/iiAgentApplicationInterfaceConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiAgentApplicationInterfaceConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/iiAgentApplicationInterfaceConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiAgentApplicationInterface)
install(FILES cmake/iiAgentApplicationInterfaceDiscovery.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiAgentApplicationInterface)
install(FILES src/python/iiagent_application_interface_discovery.py
    DESTINATION ${CMAKE_INSTALL_DATADIR}/iiAgentApplicationInterface/python)
if(IIAGENTAPPLICATIONINTERFACE_BUILD_PYTHON)
    install(FILES src/python/iiagent_application_interface.py src/python/iiagent_application_interface_native.py
        src/python/iiagent_application_interface_client.py src/python/iiagent_application_interface_cli.py
        src/python/iiagent_application_mcp.py src/python/iiagent_application_command_cli.py
        DESTINATION ${CMAKE_INSTALL_DATADIR}/iiAgentApplicationInterface/python)
    if(UNIX)
        file(RELATIVE_PATH _iiagent_bin_to_python "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}"
            "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/iiAgentApplicationInterface/python")
        configure_file(cmake/iiagent-command.in "${CMAKE_CURRENT_BINARY_DIR}/iiagent-command" @ONLY)
        configure_file(cmake/iiagent-mcp.in "${CMAKE_CURRENT_BINARY_DIR}/iiagent-mcp" @ONLY)
        install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/iiagent-command" "${CMAKE_CURRENT_BINARY_DIR}/iiagent-mcp"
            DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()
install(FILES README.md AGENT.md LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/iiAgentApplicationInterface)
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DATADIR}/iiAgentApplicationInterface/docs)
install(DIRECTORY example/ DESTINATION ${CMAKE_INSTALL_DATADIR}/iiAgentApplicationInterface/example)
