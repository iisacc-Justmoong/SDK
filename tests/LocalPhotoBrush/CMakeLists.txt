cmake_minimum_required(VERSION 3.24)
project(LocalPhotoBrush LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(iiPaintEngine 0.4.2 CONFIG REQUIRED)
find_package(iiGraphicAdjust 0.5.1 CONFIG REQUIRED)
include(CTest)

add_executable(LocalPhotoBrush demo.cpp)
target_link_libraries(LocalPhotoBrush PRIVATE
    iiPaintEngine::iiPaintEngine iiGraphicAdjust::iiGraphicAdjust)
target_compile_features(LocalPhotoBrush PRIVATE cxx_std_23)
if(BUILD_TESTING)
    add_executable(LocalPhotoBrushContracts contracts.cpp)
    target_link_libraries(LocalPhotoBrushContracts PRIVATE
        iiPaintEngine::iiPaintEngine iiGraphicAdjust::iiGraphicAdjust)
    add_test(NAME LocalPhotoBrush.integration COMMAND LocalPhotoBrushContracts)
    add_test(NAME LocalPhotoBrush.example COMMAND LocalPhotoBrush "${CMAKE_CURRENT_BINARY_DIR}/images")
    set_tests_properties(LocalPhotoBrush.integration LocalPhotoBrush.example PROPERTIES TIMEOUT 120)

    set(PHOTOSHOP_RECONSTRUCTION_ROOT "" CACHE PATH "Optional pinned workspace reconstruction for comparison")
    if(PHOTOSHOP_RECONSTRUCTION_ROOT)
        add_executable(LocalPhotoBrushSampleComparison sample_comparison.cpp
            "${PHOTOSHOP_RECONSTRUCTION_ROOT}/src/Filters.cpp")
        target_include_directories(LocalPhotoBrushSampleComparison PRIVATE
            "${PHOTOSHOP_RECONSTRUCTION_ROOT}/include")
        target_link_libraries(LocalPhotoBrushSampleComparison PRIVATE iiGraphicAdjust::iiGraphicAdjust)
        if(MSVC)
            target_compile_options(LocalPhotoBrushSampleComparison PRIVATE /fp:strict)
        else()
            target_compile_options(LocalPhotoBrushSampleComparison PRIVATE -fno-fast-math -ffp-contract=off)
        endif()
        add_test(NAME LocalPhotoBrush.pinned-samples COMMAND LocalPhotoBrushSampleComparison)
        set_tests_properties(LocalPhotoBrush.pinned-samples PROPERTIES TIMEOUT 120)
    endif()
    if(APPLE)
        # A login shell may globally prefer an older installed SDK over the
        # selected package's rpaths. Validate the selected packages independently.
        get_property(_local_photo_tests DIRECTORY PROPERTY TESTS)
        set_tests_properties(${_local_photo_tests} PROPERTIES ENVIRONMENT_MODIFICATION
            "DYLD_LIBRARY_PATH=unset:;DYLD_FRAMEWORK_PATH=unset:")
    endif()
endif()
