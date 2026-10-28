file(GLOB TESTS_SOURCES *.cpp Detour/*.cpp Recast/*.cpp RecastDemo/*.cpp)
# The AzerothCore mmtile reader behind RecastDemo's "Load mmtile" (no SDL/OpenGL needed).
list(APPEND TESTS_SOURCES ../RecastDemo/Source/MmapTile.cpp)

include_directories(../Detour/Include)
include_directories(../Recast/Include)
include_directories(../RecastDemo/Include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(Tests ${TESTS_SOURCES})
add_dependencies(Tests Recast Detour)
target_link_libraries(Tests Recast Detour)
add_test(Tests Tests)

install(TARGETS Tests RUNTIME DESTINATION bin)
