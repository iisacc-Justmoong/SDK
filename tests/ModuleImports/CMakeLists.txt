cmake_minimum_required(VERSION 3.24)
project(IiSdkModuleImports LANGUAGES CXX)
include(CTest)

find_package(iiSDKCore CONFIG REQUIRED COMPONENTS Contracts Runtime)

add_executable(module_contracts core_contracts.cpp)
target_link_libraries(module_contracts PRIVATE iiSDKCore::Contracts)
add_test(NAME ModuleImports.CoreContracts COMMAND module_contracts)

add_executable(module_runtime core_runtime.cpp)
target_link_libraries(module_runtime PRIVATE iiSDKCore::Runtime)
add_test(NAME ModuleImports.CoreRuntime COMMAND module_runtime)

option(IISDK_TEST_NATIVE_UI "Compile the installed GUI and LVUI module imports" ON)
if(IISDK_TEST_NATIVE_UI)
    find_package(iiGUI CONFIG REQUIRED)
    find_package(LVUI CONFIG REQUIRED COMPONENTS Native)

    add_library(module_painter OBJECT gui_painter.cpp)
    target_link_libraries(module_painter PRIVATE iiGUI::Painter)

    add_library(module_lvui OBJECT lvui_native.cpp)
    target_link_libraries(module_lvui PRIVATE LVUI::Native)

    lvui_generate(NAME ModuleImportView SOURCE view.lvui OUTPUT_VARIABLE generated_view)
    add_library(module_view OBJECT native_view.cpp "${generated_view}")
    target_include_directories(module_view PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(module_view PRIVATE iiGUI::iiGUI LVUI::Native)
endif()
