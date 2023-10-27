# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.*)

set(include_dirs
    "./"
)

idf_component_register(
    SRCS "${app_sources}"
    INCLUDE_DIRS "${include_dirs}"
    REQUIRES "${requires}"
    PRIV_REQUIRES "${priv_requires}"
)

