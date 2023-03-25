cmake_minimum_required(VERSION 3.24)
project(OperatingSystemsClass)
set(CMAKE_CXX_STANDARD 23)

find_package(glfw3 REQUIRED )
find_package(OpenGL REQUIRED)

add_executable(OperatingSystemsClass main.cpp external/glad.c)

target_link_libraries(OperatingSystemsClass  glfw OpenGL::GL)

