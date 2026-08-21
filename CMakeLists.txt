cmake_minimum_required(VERSION 3.21)
project(tsuzuki CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(LibtorrentRasterbar CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)

# Anitomy has no vcpkg port and ships no usable CMake target, so vendor it.
include(FetchContent)
FetchContent_Declare(anitomy
  GIT_REPOSITORY https://github.com/erengy/anitomy.git
  GIT_TAG        master)
FetchContent_GetProperties(anitomy)
if(NOT anitomy_POPULATED)
  FetchContent_Populate(anitomy)
endif()
file(GLOB ANITOMY_SOURCES ${anitomy_SOURCE_DIR}/anitomy/*.cpp)
add_library(anitomy STATIC ${ANITOMY_SOURCES})
target_include_directories(anitomy PUBLIC ${anitomy_SOURCE_DIR})

# Everything except the entry points, shared by the console tool and the app.
add_library(tsuzuki_core STATIC
  src/scan.cpp
  src/http.cpp
  src/anilist.cpp
  src/sources/sources.cpp
  src/ui.cpp
  src/player.cpp
  src/track.cpp
  src/discord.cpp
)
target_include_directories(tsuzuki_core PUBLIC src)
target_link_libraries(tsuzuki_core PUBLIC
  LibtorrentRasterbar::torrent-rasterbar
  CURL::libcurl
  nlohmann_json::nlohmann_json
  pugixml::pugixml
  httplib::httplib
  anitomy
)

# Console tool.
#
# Named tsuzuki-cli, not tsuzuki: Windows filenames are case-insensitive, so
# "tsuzuki.exe" and the app's "Tsuzuki.exe" are the same file and the two
# targets silently overwrite each other in the same output directory.
add_executable(tsuzuki src/main.cpp)
target_link_libraries(tsuzuki PRIVATE tsuzuki_core)
set_target_properties(tsuzuki PROPERTIES OUTPUT_NAME "tsuzuki-cli")

# Native windowed app: same engine, Win32 window hosting a WebView2 control.
if(WIN32)
  find_package(unofficial-webview2 CONFIG REQUIRED)
  add_executable(tsuzuki-app WIN32 src/app_main.cpp assets/tsuzuki.rc)
  target_link_libraries(tsuzuki-app PRIVATE
    tsuzuki_core
    unofficial::webview2::webview2
    dwmapi
    shell32
  )
  set_target_properties(tsuzuki-app PROPERTIES OUTPUT_NAME "Tsuzuki")
endif()

if(MSVC)
  target_compile_options(tsuzuki_core PRIVATE /utf-8 /W4)
  target_compile_options(tsuzuki PRIVATE /utf-8 /W4)
  if(WIN32)
    target_compile_options(tsuzuki-app PRIVATE /utf-8 /W4)
  endif()
endif()
