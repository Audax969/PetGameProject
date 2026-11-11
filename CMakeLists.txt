cmake_minimum_required(VERSION 3.21)
project(PetGameProject)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

list(PREPEND CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.12.0/macos")

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)

add_executable(PetGameProject main.cpp)
target_link_libraries(PetGameProject PRIVATE Qt6::Widgets Qt6::Svg)
target_compile_definitions(PetGameProject PRIVATE PETGAME_ASSET_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

add_custom_command(TARGET PetGameProject POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:PetGameProject>/assets)

