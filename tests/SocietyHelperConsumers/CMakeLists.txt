cmake_minimum_required(VERSION 3.24)
project(SocietyHelperConsumers LANGUAGES CXX)
include(CTest)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(iiSocietyHelper 0.9.0 CONFIG REQUIRED)
find_package(iiSocietyGeneration CONFIG REQUIRED)
find_package(iiLocalDiffusion CONFIG REQUIRED)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test Gui Qml Concurrent)

get_filename_component(workspace "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(society "${workspace}/Product/Society")
set(dreamscapes "${workspace}/Product/Dreamscapes")
set(generation "${dreamscapes}/src/App/Generation")

# Compile the real migrated application headers and account wrapper. No app
# bundle, deployment step or existing product build cache is involved.
add_executable(SocietyHelperHeaders headers.cpp
    "${society}/src/App/Account/AccountController.cpp"
    "${society}/src/App/Account/AccountController.h")
target_include_directories(SocietyHelperHeaders PRIVATE "${society}/src")
target_compile_definitions(SocietyHelperHeaders PRIVATE SOCIETY_HEADLESS)
target_link_libraries(SocietyHelperHeaders PRIVATE iiSocietyHelper::iiSocietyHelper Qt6::Qml)
add_test(NAME SocietyHelperConsumers.headers COMMAND SocietyHelperHeaders)
set_tests_properties(SocietyHelperConsumers.headers PROPERTIES ENVIRONMENT "SOCIETY_DISABLE_SESSION_RESTORE=1")

add_executable(SocietyGroupState "${society}/tests/tst_groupstate.cpp")
target_include_directories(SocietyGroupState PRIVATE "${society}/src")
target_compile_definitions(SocietyGroupState PRIVATE SOCIETY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(SocietyGroupState PRIVATE iiSocietyHelper::iiSocietyHelper Qt6::Test)
add_test(NAME SocietyHelperConsumers.group_state COMMAND SocietyGroupState)
set_tests_properties(SocietyHelperConsumers.group_state PROPERTIES TIMEOUT 60)

add_library(MigratedGeneration STATIC
    "${generation}/AdvancedImageParameters.cpp"
    "${generation}/ImageParameterCodec.cpp"
    "${generation}/AdvancedImageOutput.cpp"
    "${generation}/AdvancedImageInputs.cpp"
    "${generation}/VideoGeneration.cpp"
    "${generation}/GenerationController.cpp"
    "${generation}/GenerationController.h"
    "${generation}/ModelPreferences.cpp"
    "${generation}/SocietyGenerationStorage.cpp"
    "${generation}/ImageCanvas.cpp")
target_include_directories(MigratedGeneration PUBLIC "${generation}")
target_compile_definitions(MigratedGeneration PRIVATE
    DREAMSCAPES_FFMPEG_EXECUTABLE="" DREAMSCAPES_FFPROBE_EXECUTABLE=""
    DREAMSCAPES_DIFFUSION_EXECUTABLE="" DREAMSCAPES_DIFFUSION_PYTHON_EXECUTABLE="")
target_link_libraries(MigratedGeneration PUBLIC iiSocietyHelper::iiSocietyHelper
    iiSocietyGeneration::iiSocietyGeneration iiLocalDiffusion::iiLocalDiffusion
    Qt6::Gui Qt6::Qml Qt6::Concurrent)
set(watermark "${dreamscapes}/resources/Appicon/generated/common/Dreamscapes.png")
set_source_files_properties("${watermark}" PROPERTIES QT_RESOURCE_ALIAS "Dreamscapes.png")
qt_add_resources(MigratedGeneration watermark PREFIX "/generation/watermark" FILES "${watermark}")
add_executable(DreamscapesHelperHost "${generation}/tst_LocalSociety.cpp")
target_link_libraries(DreamscapesHelperHost PRIVATE MigratedGeneration iiSocietyGeneration::Host Qt6::Test)
target_compile_definitions(DreamscapesHelperHost PRIVATE
    DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
    DREAMSCAPES_FAKE_GENERATOR="${generation}/fixtures/generate.py")
add_test(NAME SocietyHelperConsumers.dreamscapes_host COMMAND DreamscapesHelperHost
    initialMirrorStaysUnavailableUntilSocietyPublishesIt
    nativePreviewsPublishRealPixelsAcrossPassesAndCleanUp
    generatesOnHostBeforeBackgroundModelDownloadThenUsesLocalCache)
set_tests_properties(SocietyHelperConsumers.dreamscapes_host PROPERTIES
    TIMEOUT 90 ENVIRONMENT "SOCIETY_DISABLE_SESSION_RESTORE=1;SOCIETY_CONTAINER_PATH=${CMAKE_CURRENT_BINARY_DIR}/unselected.societycontainer")
