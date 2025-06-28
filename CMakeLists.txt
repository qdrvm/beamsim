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
  -Werror=switch
)

include(FetchContent)

FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG 0.8.0
)
FetchContent_MakeAvailable(yaml-cpp)

FetchContent_Declare(
        xxHash
        GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
        GIT_TAG v0.8.2
        SOURCE_SUBDIR cmake_unofficial
)
FetchContent_MakeAvailable(xxHash)

add_executable(beamsim main.cpp)
target_include_directories(beamsim PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(beamsim yaml-cpp::yaml-cpp xxhash)

find_package(ns3 CONFIG)
if(ns3_FOUND)
  find_package(MPI REQUIRED)

  target_compile_definitions(beamsim PRIVATE ns3_FOUND)
  target_link_libraries(beamsim
    ns3::applications
    ns3::core
    ns3::internet
    ns3::point-to-point
  )
else()
  message("ns3 not found")
endif()

add_executable(gml2bin gml2bin.cpp)
target_include_directories(gml2bin PRIVATE ${PROJECT_SOURCE_DIR}/src)
