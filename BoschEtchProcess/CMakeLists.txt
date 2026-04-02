cmake_minimum_required(VERSION 3.16)
project(BoschEtchProcess LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_executable(BoschEtchProcess
    src/main.cpp
    src/Mesh.cpp
    src/simulation.cpp
    src/structures.cpp
    src/glad.c
    src/shader.cpp
    src/Measurments.cpp

    imgui/backend/imgui_impl_opengl3.cpp
    imgui/backend/imgui_impl_glfw.cpp
    
)

target_include_directories(BoschEtchProcess PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/imgui/backend
)

target_compile_definitions(BoschEtchProcess PRIVATE
    PROJECT_ROOT="${PROJECT_SOURCE_DIR}"
    IMGUI_IMPL_OPENGL_LOADER_GLAD

)

target_link_libraries(BoschEtchProcess PRIVATE
    glfw
    imgui::imgui
)