# (c) Alexander Voitenko 2021 - present

cmake_minimum_required(VERSION 3.5.0)

project(twitch-streamer VERSION 1.0.0 LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

find_package(PkgConfig)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)

set(TWITCH_STREAMER_SOURCE_FILES
    source/Main.c
)

add_executable(${PROJECT_NAME} ${TWITCH_STREAMER_SOURCE_FILES})
target_include_directories(
    ${PROJECT_NAME}
    PRIVATE
    ${GLIB_INCLUDE_DIRS}
    ${GSTREAMER_INCLUDE_DIRS}
)
target_link_libraries(
    ${PROJECT_NAME}
    ${GSTREAMER_LIBRARIES}
)