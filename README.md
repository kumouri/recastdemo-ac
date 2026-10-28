recastdemo-ac: RecastDemo for AzerothCore mmaps
===============================================

This is a fork of [jackpoz/recastnavigation](https://github.com/jackpoz/recastnavigation) at
`feature/tc-settings-enabled` (the RecastDemo that AzerothCore's `mmaps-config.yaml` recommends),
with **"Load mmtile" fixed for AzerothCore's MMAP v20 tiles**. Integration is on `develop`
(Git Flow).

**What changed.** jackpoz's loader reads TrinityCore's 20-byte `MmapTileHeader`. AzerothCore v20
writes 56 bytes: those 20, then a 36-byte `MmapTileRecastConfig`. So every tile was read 36 bytes
early, `addTile` failed the `DNAV` check, and nothing loaded, with no error. All three loaders
(Solo Mesh, Tile Mesh, Tile Mesh sub-tiles) now go through `RecastDemo/Source/MmapTile.cpp`,
which:
- reads the exact v20 header, with `static_assert`s on 56 and 36 bytes;
- **refuses** any other MMAP version, Detour version, magic or file length with an error in
  the log, and fails the whole load rather than skipping a tile;
- checks each blob's size against `dtNavMesh::addTile`'s layout arithmetic, so a
  `DT_POLYREF64` mismatch can't mis-slice a tile silently. 64-bit polyrefs are `#define`d in
  `Detour/Include/DetourNavMesh.h` and `static_assert`ed;
- reads `.mmap` as exactly one 28-byte `dtNavMeshParams`.

**Build (Windows, VS 2022 x64):** `pwsh -NoProfile -File scripts/build-windows.ps1`. It fetches
SDL2 2.32.10 (SHA-256 checked) into `RecastDemo/Contrib/SDL`, then builds `RecastDemo`,
`mmtile_loadcheck` and `Tests` into `build/cmake/`. CMake ≥ 3.5 (4.x works).

**Use.** Run the generator with `debugOutput: true` for one tile. Copy `meshes/mapMMMXXYY.obj`
and the map's `MMM.mmap` and `MMMXXYY.mmtile` files into RecastDemo's `Meshes/`. Choose the OBJ
as Input Mesh, then press **Load mmtile**. Tile Mesh loads the tile and its eight neighbours;
Solo Mesh loads the one tile. The file name `map<MMM><XX><YY>.obj` picks the tiles. A refused
tile opens the log with the reason.

**Headless proof:** `mmtile_loadcheck <mmaps dir> <map> [--expect-tiles N] [--expect-polys N]
[--expect-cross-links N] [--path wowX wowY wowZ wowX wowY wowZ]`. It uses the same reader and
loads every tile of a map. On AzerothCore map 609 (`wowgaming/client-data` v20):

```
Detour: DT_NAVMESH_VERSION=7 sizeof(dtPolyRef)=8 sizeof(dtLink)=16 MmapTileHeader=56 bytes (MMAP v20)
map 609: files=37 tiles=37 (maxTiles=42) polys=153493 verts=253742 links=354136 crossTileLinks=4338 offMeshCons=0
path: 30 polys, 9 corners, 164.0 yards, reached=yes
OK
```

A real tile with its version patched to 19, or cut down to TrinityCore's 20-byte header, gets
`REFUSED: ... is MMAP version 19` (or 15) and exit 2. `Tests/RecastDemo/Tests_MmapTile.cpp`
covers the same cases on a tile built by `dtCreateNavMeshData`.

**Safety.** Point RecastDemo and `mmtile_loadcheck` at a copy of the data; both only read it.
Putting edited tiles on a live server is a separate step. Navmesh editing (patch files applied at
bake time) lives in the companion repo `wow-mmaps-viewer`.

---


Recast & Detour
===============

[![Travis (Linux) Build Status](https://travis-ci.org/recastnavigation/recastnavigation.svg?branch=master)](https://travis-ci.org/recastnavigation/recastnavigation)
[![Appveyor (Windows) Build  Status](https://ci.appveyor.com/api/projects/status/20w84u25b3f8h179/branch/master?svg=true)](https://ci.appveyor.com/project/recastnavigation/recastnavigation/branch/master)

[![Issue Stats](http://www.issuestats.com/github/recastnavigation/recastnavigation/badge/pr?style=flat)](http://www.issuestats.com/github/recastnavigation/recastnavigation)
[![Issue Stats](http://www.issuestats.com/github/recastnavigation/recastnavigation/badge/issue?style=flat)](http://www.issuestats.com/github/recastnavigation/recastnavigation)

![screenshot of a navmesh baked with the sample program](/RecastDemo/screenshot.png?raw=true)

## Recast

Recast is state of the art navigation mesh construction toolset for games.

* It is automatic, which means that you can throw any level geometry at it and you will get robust mesh out
* It is fast which means swift turnaround times for level designers
* It is open source so it comes with full source and you can customize it to your heart's content. 

The Recast process starts with constructing a voxel mold from a level geometry 
and then casting a navigation mesh over it. The process consists of three steps, 
building the voxel mold, partitioning the mold into simple regions, peeling off 
the regions as simple polygons.

1. The voxel mold is built from the input triangle mesh by rasterizing the triangles into a multi-layer heightfield. Some simple filters are  then applied to the mold to prune out locations where the character would not be able to move.
2. The walkable areas described by the mold are divided into simple overlayed 2D regions. The resulting regions have only one non-overlapping contour, which simplifies the final step of the process tremendously.
3. The navigation polygons are peeled off from the regions by first tracing the boundaries and then simplifying them. The resulting polygons are finally converted to convex polygons which makes them perfect for pathfinding and spatial reasoning about the level. 


## Detour

Recast is accompanied with Detour, path-finding and spatial reasoning toolkit. You can use any navigation mesh with Detour, but of course the data generated with Recast fits perfectly.

Detour offers simple static navigation mesh which is suitable for many simple cases, as well as tiled navigation mesh which allows you to plug in and out pieces of the mesh. The tiled mesh allows you to create systems where you stream new navigation data in and out as the player progresses the level, or you may regenerate tiles as the world changes. 


## Recast Demo

You can find a comprehensive demo project in RecastDemo folder. It is a kitchen sink demo containing all the functionality of the library. If you are new to Recast & Detour, check out [Sample_SoloMesh.cpp](/RecastDemo/Source/Sample_SoloMesh.cpp) to get started with building navmeshes and [NavMeshTesterTool.cpp](/RecastDemo/Source/NavMeshTesterTool.cpp) to see how Detour can be used to find paths.

### Building RecastDemo

RecastDemo uses [premake5](http://premake.github.io/) to build platform specific projects. Download it and make sure it's available on your path, or specify the path to it.

#### Linux

- Install SDL2 and its dependencies according to your distro's guidelines.
- run `premake5 gmake` from the `RecastDemo` folder.
- `cd Build/gmake` then `make`
- Run `RecastDemo\Bin\RecastDemo`

#### OSX

- Grab the latest SDL2 development library dmg from [here](https://www.libsdl.org/download-2.0.php) and place `SDL2.framework` in `/Library/Frameworks/`
- Navigate to the `RecastDemo` folder and run `premake5 xcode4`
- Open `Build/xcode4/recastnavigation.xcworkspace`
- Select the "RecastDemo" project in the left pane, go to the "BuildPhases" tab and expand "Link Binary With Libraries"
- Remove the existing entry for SDL2 (it should have a white box icon) and re-add it by hitting the plus, selecting "Add Other", and selecting `/Library/Frameworks/SDL2.framework`.  It should now have a suitcase icon.
- Set the RecastDemo project as the target and build.

#### Windows

- Grab the latest SDL2 development library release from [here](https://www.libsdl.org/download-2.0.php) and unzip it `RecastDemo\Contrib`.  Rename the SDL folder such that the path `RecastDemo\Contrib\SDL\lib\x86` is valid.
- Run `"premake5" vs2015` from the `RecastDemo` folder
- Open the solution, build, and run.

### Running Unit tests

- Follow the instructions to build RecastDemo above.  Premake should generate another build target called "Tests".
- Build the "Tests" project.  This will generate an executable named "Tests" in `RecastDemo/Bin/`
- Run the "Tests" executable.  It will execute all the unit tests, indicate those that failed, and display a count of those that succeeded.

## Integrating with your own project

It is recommended to add the source directories `DebugUtils`, `Detour`, `DetourCrowd`, `DetourTileCache`, and `Recast` into your own project depending on which parts of the project you need. For example your level building tool could include `DebugUtils`, `Recast`, and `Detour`, and your game runtime could just include `Detour`.

## Contributing

See the [Contributing document](CONTRIBUTING.md) for guidelines for making contributions.

## Discuss

- Discuss Recast & Detour: http://groups.google.com/group/recastnavigation
- Development blog: http://digestingduck.blogspot.com/

## License

Recast & Detour is licensed under ZLib license, see License.txt for more information.
