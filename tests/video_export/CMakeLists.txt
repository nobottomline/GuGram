cmake_minimum_required(VERSION 3.21)
project(VideoExportTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(Qt6 REQUIRED COMPONENTS Core Gui)
enable_testing()
add_executable(video_export_files_test
    files_test.cpp
    ../../Telegram/SourceFiles/export/export_video_files.cpp)
target_include_directories(video_export_files_test PRIVATE ../../Telegram/SourceFiles)
target_link_libraries(video_export_files_test PRIVATE Qt6::Core)
add_test(NAME video_export_files COMMAND video_export_files_test)

add_executable(video_export_status_test
    status_test.cpp
    ../../Telegram/SourceFiles/export/export_video_status.cpp
    ../../Telegram/SourceFiles/export/export_video_badge.cpp
    ../../Telegram/SourceFiles/export/export_video_files.cpp)
target_include_directories(video_export_status_test PRIVATE ../../Telegram/SourceFiles)
target_link_libraries(video_export_status_test PRIVATE Qt6::Core Qt6::Gui)
add_test(NAME video_export_status COMMAND video_export_status_test)
