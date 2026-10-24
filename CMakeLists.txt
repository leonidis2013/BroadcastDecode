cmake_minimum_required(VERSION 3.16)
project(BroadcastDecode VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(DRM REQUIRED libdrm)
pkg_check_modules(ALSA REQUIRED alsa)
find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)

# The NDI SDK is not stored in this repo. Point NDI_SDK_DIR at the folder that
# contains include/ and lib/ (see docs/BUILD.md).
set(NDI_SDK_DIR "" CACHE PATH "Path to the extracted NDI SDK for Linux")
set(NDI_LIB_ARCH "aarch64-rpi4-linux-gnueabi" CACHE STRING "Subfolder of NDI_SDK_DIR/lib to link against")
if(NOT NDI_SDK_DIR OR NOT EXISTS "${NDI_SDK_DIR}/include/Processing.NDI.Lib.h")
  message(FATAL_ERROR "Set -DNDI_SDK_DIR=<path to NDI SDK> (Processing.NDI.Lib.h not found)")
endif()

add_executable(bdecode-player
  src/main.cpp
  src/config.cpp
  src/display.cpp
  src/audio.cpp)

target_compile_definitions(bdecode-player PRIVATE BDECODE_VERSION="${PROJECT_VERSION}")
target_compile_options(bdecode-player PRIVATE -Wall -Wextra)
target_include_directories(bdecode-player PRIVATE
  src
  ${NDI_SDK_DIR}/include
  ${DRM_INCLUDE_DIRS}
  ${ALSA_INCLUDE_DIRS})
target_link_directories(bdecode-player PRIVATE ${NDI_SDK_DIR}/lib/${NDI_LIB_ARCH})
target_link_libraries(bdecode-player PRIVATE
  ndi
  ${DRM_LIBRARIES}
  ${ALSA_LIBRARIES}
  nlohmann_json::nlohmann_json
  Threads::Threads)

install(TARGETS bdecode-player RUNTIME DESTINATION bin)
