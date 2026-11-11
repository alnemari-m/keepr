cmake_minimum_required(VERSION 3.20)
project(keepr VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(CURL REQUIRED)
find_package(nlohmann_json 3.10 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(json)
endif()

# Core library: everything except the CLI entry point, so tests can link it.
add_library(keepr_core STATIC
  src/util.cpp
  src/http.cpp
  src/url.cpp
  src/video.cpp
  src/x_render.cpp
  src/x_download.cpp
)
target_include_directories(keepr_core PUBLIC src)
target_link_libraries(keepr_core PUBLIC CURL::libcurl nlohmann_json::nlohmann_json)
target_compile_options(keepr_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(keepr src/main.cpp)
target_link_libraries(keepr PRIVATE keepr_core)
target_compile_options(keepr PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS keepr RUNTIME DESTINATION bin)

enable_testing()
add_executable(keepr_tests tests/test_main.cpp)
target_link_libraries(keepr_tests PRIVATE keepr_core)
target_compile_definitions(keepr_tests PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_test(NAME keepr_tests COMMAND keepr_tests)

# ---- Qt 6 desktop GUI (optional) ----
option(KEEPR_GUI "Build the Qt 6 desktop GUI" ON)
if(KEEPR_GUI)
  find_package(Qt6 6.2 QUIET COMPONENTS Widgets)
  if(Qt6_FOUND)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    add_executable(keepr-gui gui/main.cpp gui/MainWindow.cpp gui/MainWindow.hpp gui/resources.qrc)
    target_link_libraries(keepr-gui PRIVATE Qt6::Widgets)
    target_compile_options(keepr-gui PRIVATE -Wall -Wextra)
    install(TARGETS keepr-gui RUNTIME DESTINATION bin)
    install(FILES gui/keepr.desktop DESTINATION share/applications)
    install(FILES gui/icons/keepr.svg DESTINATION share/icons/hicolor/scalable/apps)
    install(FILES gui/icons/keepr.png DESTINATION share/icons/hicolor/256x256/apps)
  else()
    message(STATUS "Qt 6 Widgets not found: skipping keepr-gui")
  endif()
endif()
