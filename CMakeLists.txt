cmake_minimum_required(VERSION 3.25)

project(beamsim)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(
  -Wall
  -Wextra
  -Woverloaded-virtual
  -Wnull-dereference
  -Werror=unused-lambda-capture
  -Werror=return-type
  -Werror=sign-compare
  -Werror=mismatched-tags
  -Werror-switch
)

add_executable(main main.cpp)
target_include_directories(main PRIVATE ${PROJECT_SOURCE_DIR}/src)
