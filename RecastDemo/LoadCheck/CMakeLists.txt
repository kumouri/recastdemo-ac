# Headless mmtile load check: the same reader as RecastDemo's "Load mmtile" button
# (Source/MmapTile.cpp), without SDL or OpenGL, so it can run in CI and on servers.
add_executable(mmtile_loadcheck MmtileLoadCheck.cpp ../Source/MmapTile.cpp)
target_include_directories(mmtile_loadcheck PRIVATE
	../Include ../../Detour/Include ../../Recast/Include)
set_target_properties(mmtile_loadcheck PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mmtile_loadcheck Detour Recast)

install(TARGETS mmtile_loadcheck RUNTIME DESTINATION bin)
