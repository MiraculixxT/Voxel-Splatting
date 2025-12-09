cmake_minimum_required(VERSION 3.14)
project(VoxelSplatting VERSION 1.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- Find OpenGL ---
find_package(OpenGL REQUIRED)

# --- Add Libraries using FetchContent (easier setup) ---
include(FetchContent)

# GLFW (Windowing & Input)
FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
)
FetchContent_MakeAvailable(glfw)

# GLM (Math Library)
FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        a532f5b1cf27d6a3c099437e6959cf7e398a0a67 # 1.02
)
FetchContent_MakeAvailable(glm)

# ImGui (Debug UI)
FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.92.4
)
FetchContent_MakeAvailable(imgui)

# stb (for image loading)
FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG        master
)
FetchContent_MakeAvailable(stb)

# --- Add GLAD (OpenGL Loader) ---
# NOTE: You must generate and download GLAD from https://glad.dav1d.de/
# Settings: C/C++, Core, OpenGL 4.1 (or higher)
# Unzip it and place the 'include' and 'src' folders into a 'vendor/glad' directory
add_library(glad STATIC vendor/glad/src/glad.c
        include/render/gl/GLSplatRenderer.hpp
        include/render/gl/GLShadowMap.hpp)
target_include_directories(glad PUBLIC vendor/glad/include)

# --- Include Directories ---
include_directories(
        ${OPENGL_INCLUDE_DIR}
        ${glm_SOURCE_DIR}
        ${stb_SOURCE_DIR}
        vendor/glad/include
        src
)

# --- Build ImGui from source (core) ---
add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})

# --- ImGui backends (GLFW + OpenGL3) ---
add_library(imgui_impl STATIC
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_impl PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_impl PUBLIC imgui glfw ${OPENGL_LIBRARIES})


# --- Define Executable ---
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE GAME_HEADERS CONFIGURE_DEPENDS "include/*.h" "include/*.hpp")
add_executable(VoxelSplatting
        ${GAME_SOURCES}
        ${GAME_HEADERS}
        src/render/gl/GLSplatRenderer.cpp
        src/render/gl/GLShadowMap.cpp
)
target_include_directories(VoxelSplatting PRIVATE include)

# --- Link Libraries ---
target_link_libraries(VoxelSplatting PUBLIC
        glad
        glfw
        ${OPENGL_LIBRARIES}
        imgui_impl
        glm
)

# Set output directory for convenience
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# DEBUG
#if (CMAKE_BUILD_TYPE STREQUAL "Debug")
#    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer")
#    set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=address,undefined")
#endif()
