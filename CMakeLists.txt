project(cnbt)

cmake_minimum_required(VERSION 2.6)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

IF (CMAKE_COMPILER_IS_GNUCXX)
  set(CMAKE_C_FLAGS "-Wcpp -Wall -W -Wextra -fstrict-aliasing -Wstrict-aliasing=3 -pedantic -std=c99 -Wunsafe-loop-optimizations -funsafe-loop-optimizations")
  set(CMAKE_C_FLAGS_DEBUG "-DDEBUG -g")
  set(CMAKE_C_FLAGS_PROFILE "-DDEBUG -g -pg")
  set(CMAKE_C_FLAGS_RELEASE "-march=native -O3 -s -DNDEBUG")
ENDIF()

# Output paths
set(EXECUTABLE_OUTPUT_PATH bin)

ADD_LIBRARY(nbt buffer.c
  nbt_loading.c
  nbt_parsing.c
  nbt_treeops.c
  nbt_util.c
)
