# This file is automatically generated by running
#
#     cmake/scripts/generate-cmakefiles
#
# Require CMake 2.8
cmake_minimum_required(VERSION 2.8)

set(PROJECT_NAME demo_poisson)
project(${PROJECT_NAME})

# Set verbose output while testing CMake
#set(CMAKE_VERBOSE_MAKEFILE 1)

# Set CMake behavior
cmake_policy(SET CMP0004 OLD)

# Get DOLFIN configuration data (DOLFINConfig.cmake must be in DOLFIN_CMAKE_CONFIG_PATH)
find_package(DOLFIN)

# Default build type (can be overridden by user)
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING
    "Choose the type of build, options are: Debug MinSizeRel Release RelWithDebInfo." FORCE)
endif()

# Compiler definitions
add_definitions(${DOLFIN_CXX_DEFINITIONS})

# Compiler flags
set(CMAKE_CXX_FLAGS "${DOLFIN_CXX_FLAGS} ${CMAKE_CXX_FLAGS}")

# Include directories
include_directories(${DOLFIN_INCLUDE_DIRS})
include_directories(SYSTEM ${DOLFIN_3RD_PARTY_INCLUDE_DIRS})

# Executable
add_executable(${PROJECT_NAME} DiscreteGradient.cpp)

# Target libraries
target_link_libraries(${PROJECT_NAME} ${DOLFIN_LIBRARIES} ${DOLFIN_3RD_PARTY_LIBRARIES})
