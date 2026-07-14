cmake_minimum_required(VERSION 3.16...3.26)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# ==============================================================================
# 🛠️ HARDCODED UI PATHS AND TOGGLES
# ==============================================================================
set(ENABLE_FRONTEND_API ON CACHE BOOL "Force UI on" FORCE)
set(ENABLE_QT ON CACHE BOOL "Force Qt on" FORCE)

set(obs-frontend-api_DIR "C:/dev/obs-studio/build/frontend/api" CACHE PATH "Hardcoded OBS API" FORCE)
set(Qt6_DIR "C:/dev/obs-studio/.deps/obs-deps-qt6-2026-06-25-x64/lib/cmake/Qt6" CACHE PATH "Hardcoded Qt6" FORCE)
# ==============================================================================

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

# ==============================================================================
# 🛠️ THE ULTIMATE SIMDe BYPASS
# ==============================================================================
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/SIMDeConfig.cmake"
    "if(NOT TARGET SIMDe::SIMDe)\n"
    "  add_library(SIMDe::SIMDe INTERFACE IMPORTED)\n"
    "  set_target_properties(SIMDe::SIMDe PROPERTIES INTERFACE_INCLUDE_DIRECTORIES \"C:/dev/obs-deps/include\")\n"
    "endif()\n"
)
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_BINARY_DIR}")
list(APPEND CMAKE_MODULE_PATH "C:/dev/obs-studio/cmake/Modules")
# ==============================================================================

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 REQUIRED COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME} PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header
                                  -Wno-comma>)
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON
               AUTOUIC ON
               AUTORCC ON)
endif()

# build cURL
set(USE_SYSTEM_CURL
    OFF
    CACHE STRING "Use system cURL")
if(USE_SYSTEM_CURL)
  find_package(CURL REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "${CURL_LIBRARIES}")
  target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PUBLIC "${CURL_INCLUDE_DIRS}")
else()
  include(cmake/BuildMyCurl.cmake)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE libcurl)
endif()

# build sherpa-onnx
include(cmake/BuildSherpaOnnx.cmake)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE sherpa-onnx)

# ==============================================================================
# 🛠️ HARDCODED LIBARCHIVE FIX
# ==============================================================================
find_package(LibArchive REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE LibArchive::LibArchive)
# ==============================================================================

if(WIN32)
  find_package(BZip2 REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE BZip2::BZip2)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE BZ_STATIC)
endif()

# ==============================================================================
# 🛠️ COMPILER OVERRIDE
# Force Visual Studio to stop treating minor warnings as fatal errors
# ==============================================================================
if(MSVC)
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /WX-)
endif()
# ==============================================================================

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE src/input-thread.cpp
          src/model-utils/model-downloader-ui.cpp
          src/model-utils/model-downloader.cpp
          src/model-utils/model-find-utils.cpp
          src/model-utils/model-infos.cpp
          src/plugin-main.c
          src/sherpa-tts/sherpa-tts.cpp
          src/audio-thread.cpp
          src/squawk-source.c
          src/squawk-source.cpp
          src/tts-utils.cpp)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})