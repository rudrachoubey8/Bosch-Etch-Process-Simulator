cmake_minimum_required(VERSION 3.16)
project(BoschEtchProcess LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)

add_executable(BoschEtchProcess
    src/main.cpp
    src/Mesh.cpp
    src/simulation.cpp
    src/structures.cpp
    src/glad.c
 "src/shader.cpp" "include/shader.h" "include/MathUtils.h")

target_compile_definitions(BoschEtchProcess PRIVATE
    PROJECT_ROOT="${PROJECT_SOURCE_DIR}"
)


target_include_directories(BoschEtchProcess PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(BoschEtchProcess PRIVATE glfw)
