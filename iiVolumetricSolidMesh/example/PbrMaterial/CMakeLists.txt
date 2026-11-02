# iisdk:consumer-application
# Detached application composition project; no SDK links another SDK.
cmake_minimum_required(VERSION 3.23)
project(PbrVolumetricApplicationComponents LANGUAGES CXX)
find_package(iiPBRMaterial CONFIG REQUIRED)
find_package(iiVolumetricSolidMesh CONFIG REQUIRED)
find_package(iiVisualCodec CONFIG REQUIRED COMPONENTS Image)
add_library(PbrApplicationComponents STATIC MaterialComposition.cpp VisualTextureDecoder.cpp)
target_compile_features(PbrApplicationComponents PUBLIC cxx_std_23)
target_include_directories(PbrApplicationComponents PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(PbrApplicationComponents PUBLIC
  iiPBRMaterial::iiPBRMaterial iiVolumetricSolidMesh::iiVolumetricSolidMesh iiVisualCodec::Image)
