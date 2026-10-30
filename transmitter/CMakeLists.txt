cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME transmitter)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

option(FHSS_ENABLE_TRACE "Enable tracing for FHSS" OFF)
option(FHSS_ENABLE_STATS "Enable stats for FHSS" OFF)
option(ENABLE_BENCHMARK "Build the evaluation benchmark suite instead of the main app" OFF)

# App
if(ENABLE_BENCHMARK)
    target_sources(${PROJECT_NAME} PRIVATE App/Src/benchmark.c)
else()
    target_sources(${PROJECT_NAME} PRIVATE App/Src/app.c)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    App/Inc
)

# Custom libraries
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../lib/nrf24l01_driver ${CMAKE_BINARY_DIR}/nrf24l01_build)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../fhss ${CMAKE_BINARY_DIR}/fhss_build)

# Third-party libraries

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    nrf24l01
    fhss_common
)
