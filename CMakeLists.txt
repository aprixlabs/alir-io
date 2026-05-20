cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

# Project name and version are automatically parsed from buildspec.json by bootstrap.cmake
project(${_name} VERSION ${_version})

include(compilerconfig)
include(defaults)
include(helpers)


# Find libobs and related dependencies
find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 COMPONENTS Widgets Core REQUIRED)

# QT support
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

add_library(${CMAKE_PROJECT_NAME} MODULE)

set(ALIRIO_QRC src/asio-input.qrc)

# Steinberg recently dual-licensed ASIO SDK under GPLv3!
# We can now legally fetch it directly from GitHub instead of forcing the user to download it manually.
include(FetchContent)
FetchContent_Declare(
  ASIOSDK
  GIT_REPOSITORY https://github.com/audiosdk/asio.git
  GIT_TAG main
)
FetchContent_MakeAvailable(ASIOSDK)
set(ASIOSDK_DIR "${asiosdk_SOURCE_DIR}")

target_sources(${CMAKE_PROJECT_NAME} PRIVATE 
  src/plugin-main.cpp
  src/asio-manager.cpp
  src/asio-input.cpp
  src/asio-output.cpp
  "${ASIOSDK_DIR}/common/asio.cpp"
  "${ASIOSDK_DIR}/common/combase.cpp"
  "${ASIOSDK_DIR}/common/register.cpp"
  "${ASIOSDK_DIR}/host/asiodrivers.cpp"
  "${ASIOSDK_DIR}/host/pc/asiolist.cpp"
  "${ASIOSDK_DIR}/host/ASIOConvertSamples.cpp"
  ${ALIRIO_QRC}
)

# Define compiler definitions needed for ASIOSDK
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE IEEE754_64FLOAT=1 _MBCS)

# Suppress deprecation warnings (e.g., circlebuf functions deprecated in OBS 31)
if(MSVC)
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /wd4996)
else()
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wno-deprecated-declarations)
endif()

# Undefine UNICODE for specific ASIOSDK files without modifying their source code
# Also disable strict string checking and size_t conversion warnings since the ASIO SDK codebase is old.
if(MSVC)
  set(UNDEF_UNICODE_FLAG "/UUNICODE" "/U_UNICODE" "/Zc:strictStrings-" "/wd4267")
else()
  set(UNDEF_UNICODE_FLAG "-UUNICODE" "-U_UNICODE" "-Wno-write-strings")
endif()
set_source_files_properties(
  "${ASIOSDK_DIR}/common/register.cpp"
  "${ASIOSDK_DIR}/host/pc/asiolist.cpp"
  PROPERTIES COMPILE_OPTIONS "${UNDEF_UNICODE_FLAG}"
)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE 
  ${CMAKE_SOURCE_DIR}/src 
  "${ASIOSDK_DIR}/common"
  "${ASIOSDK_DIR}/host"
  "${ASIOSDK_DIR}/host/pc"
)

target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE OBS::libobs
          OBS::obs-frontend-api
          Qt6::Core
          Qt6::Widgets)

# Finalize plugin target
set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
