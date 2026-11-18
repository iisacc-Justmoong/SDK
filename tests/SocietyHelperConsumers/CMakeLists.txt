cmake_minimum_required(VERSION 3.24)
project(SocietyHelperConsumers LANGUAGES CXX)
include(CTest)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(iiSocietyHelper 0.10.0 CONFIG REQUIRED)

find_package(iiSocietyHelper 0.10.0 CONFIG REQUIRED COMPONENTS GenerationHost)
find_package(iiLocalDiffusion CONFIG REQUIRED)
find_package(iiPhotoLibrary 0.1.1 CONFIG REQUIRED COMPONENTS Library)
find_package(iiSDKCore 0.18.1 CONFIG REQUIRED COMPONENTS Runtime)
find_package(iiAccountManagerNative 0.2.10 CONFIG REQUIRED)
find_package(iiFileProviderCoreIO CONFIG REQUIRED)
find_package(iiImageCodec 0.1 CONFIG REQUIRED)
find_package(iiMultimodalAgent 0.1 CONFIG REQUIRED COMPONENTS FailureDiagnostics)
find_package(iiJson 0.1 CONFIG REQUIRED)
find_package(LVUI 0.1 CONFIG REQUIRED COMPONENTS Compiler)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Gui Qml Concurrent)
# Imported package headers normally become -isystem, which follows inherited
# CPATH entries. The selected diffusion package must precede older installs.
set_property(TARGET iiLocalDiffusion::iiLocalDiffusion PROPERTY SYSTEM FALSE)
set_property(TARGET iiLocalDiffusion::ModelPackaging PROPERTY SYSTEM FALSE)

get_filename_component(workspace "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(society "${workspace}/Product/Society")
set(dreamscapes "${workspace}/Product/Dreamscapes")
set(generation "${dreamscapes}/src/App/Generation")

# Compile the real migrated application headers and account wrapper. No app
# bundle, deployment step or existing product build cache is involved.
add_executable(SocietyHelperHeaders headers.cpp
    "${society}/App/Society/Account/AccountController.cpp"
    "${society}/App/Society/Account/AccountController.h"
    "${society}/App/Society/Account/LicenseBroker.cpp"
    "${society}/App/Society/Account/LocalLicenseBroker.cpp")
target_include_directories(SocietyHelperHeaders PRIVATE "${society}/App")
target_compile_definitions(SocietyHelperHeaders PRIVATE SOCIETY_HEADLESS)
target_link_libraries(SocietyHelperHeaders PRIVATE iiSocietyHelper::iiSocietyHelper Qt6::Qml)
add_test(NAME SocietyHelperConsumers.headers COMMAND SocietyHelperHeaders)
set_tests_properties(SocietyHelperConsumers.headers PROPERTIES ENVIRONMENT "SOCIETY_DISABLE_SESSION_RESTORE=1")

add_executable(SocietyGroupState "${society}/tests/tst_groupstate.cpp")
target_include_directories(SocietyGroupState PRIVATE "${society}/App")
target_compile_definitions(SocietyGroupState PRIVATE SOCIETY_TEST_DIRECTORY="${society}/resources")
target_link_libraries(SocietyGroupState PRIVATE iiSocietyHelper::iiSocietyHelper)
add_test(NAME SocietyHelperConsumers.group_state COMMAND SocietyGroupState)
set_tests_properties(SocietyHelperConsumers.group_state PROPERTIES TIMEOUT 60)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME SocietyHelperConsumers.group_state_ctest_contract COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/test_ctest_group_state.py")
    add_test(NAME SocietyHelperConsumers.dreamscapes_ctest_contract COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/test_ctest_dreamscapes.py")
endif()

# Compile Society's actual synchronization and generation-host integration.
# An object library checks the migrated API without building an application.
add_library(SocietyNetworkCompile OBJECT
    "${society}/App/Society/Network/NetworkDriveController.cpp"
    "${society}/App/Society/Network/NetworkDriveController.h")
target_include_directories(SocietyNetworkCompile PRIVATE "${society}/App")
target_link_libraries(SocietyNetworkCompile PRIVATE iiSocietyHelper::GenerationHost
    iiPhotoLibrary::iiPhotoLibrary Qt6::Qml)

add_executable(SocietyDependencyContract "${society}/tests/tst_dependencies.cpp")
target_link_libraries(SocietyDependencyContract PRIVATE iiSocietyHelper::iiSocietyHelper iiWebServer::iiWebServer)
target_compile_definitions(SocietyDependencyContract PRIVATE
    SOCIETY_TEST_DIRECTORY="${society}/resources"
    SOCIETY_EXECUTABLE_PATH="" SOCIETY_DIRECT_LINKS_FILE="")
add_test(NAME SocietyHelperConsumers.file_projection
    COMMAND SocietyDependencyContract fileProjectionHidesProviderControlFiles)
set_tests_properties(SocietyHelperConsumers.file_projection PROPERTIES TIMEOUT 30)

add_library(MigratedGeneration STATIC
    "${generation}/AdvancedImageParameters.cpp"
    "${generation}/ImageParameterCodec.cpp"
    "${generation}/AdvancedImageOutput.cpp"
    "${generation}/AdvancedImageInputs.cpp"
    "${generation}/VideoGeneration.cpp"
    "${generation}/VideoPresets.cpp"
    "${generation}/GenerationController.cpp"
    "${generation}/GenerationController.h"
    "${generation}/SocietyAccount.cpp"
    "${generation}/SocietyAccount.h"
    "${generation}/GenerationRequests.cpp"
    "${generation}/GenerationExecution.cpp"
    "${generation}/GenerationProgress.cpp"
    "${generation}/FailureAgent.cpp"
    "${generation}/FailureAgent.h"
    "${generation}/GenerationPublication.cpp"
    "${generation}/GenerationProcess.cpp"
    "${generation}/GenerationProcess.h"
    "${generation}/NativeGenerationWork.cpp"
    "${generation}/ModelPreferences.cpp"
    "${generation}/SocietyGenerationStorage.cpp"
    "${generation}/ImageCanvas.cpp"
    "${dreamscapes}/src/App/ViewModels/ViewData.cpp"
    "${dreamscapes}/src/App/ViewModels/ViewData.h"
    "${dreamscapes}/src/App/ViewModels/LvuiStyleTokens.cpp"
    "${dreamscapes}/src/App/ViewModels/LvuiStyleTokens.h"
    "${dreamscapes}/src/App/ViewModels/ViewProjectionModel.cpp"
    "${dreamscapes}/src/App/ViewModels/ViewProjectionModel.h"
    "${dreamscapes}/src/App/ViewModels/TimelinePlaybackModel.cpp"
    "${dreamscapes}/src/App/ViewModels/TimelinePlaybackModel.h"
    "${dreamscapes}/src/App/ViewModels/VideoTimelineModel.cpp"
    "${dreamscapes}/src/App/ViewModels/VideoTimelineModel.h"
    "${dreamscapes}/src/App/ViewModels/GenerationProjection.cpp"
    "${dreamscapes}/src/App/ViewModels/GenerationProjection.h"
    "${dreamscapes}/src/App/ViewModels/SnapshotListModel.cpp"
    "${dreamscapes}/src/App/ViewModels/SnapshotListModel.h"
    "${dreamscapes}/src/App/ViewModels/Native/ViewValue.cpp"
    "${dreamscapes}/src/App/ViewModels/Native/ViewCatalog.cpp"
    "${dreamscapes}/src/App/ViewModels/Native/VideoTimeline.cpp"
    "${dreamscapes}/src/App/ViewModels/Native/EditorToolState.cpp"
    "${dreamscapes}/src/App/ViewModels/Native/GenerationPresentation.cpp"
    "${dreamscapes}/src/App/ViewModels/Native/GenerationDraft.cpp"
    "${dreamscapes}/src/App/ViewModels/Native/GraphicDraft.cpp"
    "${dreamscapes}/src/App/ViewModels/Native/ViewRuntime.cpp"
    "${dreamscapes}/src/App/ViewModels/Native/ViewProjection.cpp"
    "${dreamscapes}/src/App/ViewModels/Native/CanvasDraft.cpp"
    "${dreamscapes}/src/App/Views/Result/NativeReviewQueue.cpp"
    "${dreamscapes}/src/App/Views/Result/NativeReviewSession.cpp")
target_include_directories(MigratedGeneration PUBLIC "${generation}" "${dreamscapes}/src")
# Dependency packages can expose an older installation of the same headers.
# Prefer the explicitly selected native packages, as the application does.
target_include_directories(MigratedGeneration BEFORE PUBLIC
    "$<TARGET_PROPERTY:iiLocalDiffusion::ModelPackaging,INTERFACE_INCLUDE_DIRECTORIES>"
    "$<TARGET_PROPERTY:iiAccountManager::Native,INTERFACE_INCLUDE_DIRECTORIES>")
target_compile_definitions(MigratedGeneration PRIVATE
    DREAMSCAPES_FFMPEG_EXECUTABLE="" DREAMSCAPES_FFPROBE_EXECUTABLE=""
    DREAMSCAPES_DIFFUSION_EXECUTABLE="" DREAMSCAPES_DIFFUSION_PYTHON_EXECUTABLE="")
target_link_libraries(MigratedGeneration PUBLIC iiSocietyHelper::iiSocietyHelper
    iiLocalDiffusion::iiLocalDiffusion iiLocalDiffusion::ModelPackaging
    iiSDKCore::Runtime iiAccountManager::Native iiImageCodec::iiImageCodec
    iiMultimodalAgent::FailureDiagnostics iiJson::iiJson iiFileProvider::Temporary iiFileProvider::CoreIO LVUI::LVUI
    Qt6::Gui Qt6::Qml Qt6::Concurrent)
set(watermark "${dreamscapes}/resources/Appicon/generated/common/Dreamscapes.png")
set_source_files_properties("${watermark}" PROPERTIES QT_RESOURCE_ALIAS "Dreamscapes.png")
qt_add_resources(MigratedGeneration watermark PREFIX "/generation/watermark" FILES "${watermark}")
set(native_view_catalog_files)
foreach(catalog IN ITEMS tools text text-definition photo file compact elements graphic view-options field-groups background-defaults)
    set(path "${dreamscapes}/resources/viewmodels/${catalog}.json")
    set_source_files_properties("${path}" PROPERTIES QT_RESOURCE_ALIAS "${catalog}.json")
    list(APPEND native_view_catalog_files "${path}")
endforeach()
qt_add_resources(MigratedGeneration native_view_catalogs PREFIX "/viewmodels" FILES ${native_view_catalog_files})
add_executable(DreamscapesHelperHost dreamscapes_host_ctest.cpp)
target_link_libraries(DreamscapesHelperHost PRIVATE MigratedGeneration iiSocietyHelper::GenerationHost)
target_compile_definitions(DreamscapesHelperHost PRIVATE
    SOCIETY_HELPER_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources"
    DREAMSCAPES_FAKE_GENERATOR="${generation}/fixtures/generate.py")
foreach(case IN ITEMS
    initialMirrorStaysUnavailableUntilSocietyPublishesIt
    nativePreviewsPublishRealPixelsAcrossPassesAndCleanUp
    generatesOnHostBeforeBackgroundModelDownloadThenUsesLocalCache)
    add_test(NAME "SocietyHelperConsumers.dreamscapes_host.${case}" COMMAND DreamscapesHelperHost "${case}")
    set_tests_properties("SocietyHelperConsumers.dreamscapes_host.${case}" PROPERTIES
        TIMEOUT 90 ENVIRONMENT "SOCIETY_DISABLE_SESSION_RESTORE=1")
    # Match runtime loading to the package selected at configure time even
    # when the developer's environment names an older installed SDK first.
    if(APPLE)
        set_property(TEST "SocietyHelperConsumers.dreamscapes_host.${case}" APPEND PROPERTY ENVIRONMENT_MODIFICATION
            "DYLD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:iiLocalDiffusion::iiLocalDiffusion>")
    elseif(WIN32)
        set_property(TEST "SocietyHelperConsumers.dreamscapes_host.${case}" APPEND PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:iiLocalDiffusion::iiLocalDiffusion>")
    elseif(UNIX)
        set_property(TEST "SocietyHelperConsumers.dreamscapes_host.${case}" APPEND PROPERTY ENVIRONMENT_MODIFICATION
            "LD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:iiLocalDiffusion::iiLocalDiffusion>")
    endif()
endforeach()
