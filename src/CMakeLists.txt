cmake_minimum_required(VERSION 3.20.0)
project(befuzzled)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE befuzzled_HDR_FILES "befuzzled/*.h")
file(GLOB_RECURSE befuzzled_HDR_FILES "befuzzled/*.cpp")

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
add_executable(befuzzled WIN32 ${befuzzled_HDR_FILES} ${befuzzled_SRC_FILES})
endif()

find_package(SDL2 CONFIG REQUIRED)
find_package(skia CONFIG REQUIRED)

target_link_libraries(befuzzled
    PRIVATE
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
)
target_link_libraries(befuzzled PRIVATE skia::skia)