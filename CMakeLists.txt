cmake_minimum_required(VERSION 3.21)
project(tsuzuki CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(LibtorrentRasterbar CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)

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

add_executable(tsuzuki
  src/main.cpp
  src/scan.cpp
)

target_include_directories(tsuzuki PRIVATE src)
target_link_libraries(tsuzuki PRIVATE
  LibtorrentRasterbar::torrent-rasterbar
  CURL::libcurl
  nlohmann_json::nlohmann_json
  pugixml::pugixml
  anitomy
)

if(MSVC)
  target_compile_options(tsuzuki PRIVATE /utf-8 /W4)
endif()
