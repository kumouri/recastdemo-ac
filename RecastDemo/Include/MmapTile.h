//
// Reader for AzerothCore's server navmesh files (.mmap / .mmtile, MMAP v20).
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//

#ifndef MMAPTILE_H
#define MMAPTILE_H

#include <string>
#include "DetourNavMesh.h"

class rcContext;

// AzerothCore writes every tile blob with 64-bit polyrefs, which makes dtLink 16 bytes.
// The link array sits mid-blob, so a 32-bit Detour would mis-slice every tile silently.
static_assert(sizeof(dtPolyRef) == 8, "AzerothCore mmtiles need DT_POLYREF64 (see DetourNavMesh.h)");

namespace mmap
{

typedef unsigned int uint32;

const uint32 MMAP_MAGIC = 0x4d4d4150; // 'MMAP', on disk "PAMM"
const uint32 MMAP_VERSION = 20;       // AzerothCore src/common/Collision/Maps/MapDefines.h

// The Recast settings the generator used for this tile (MapDefines.h, 36 bytes).
struct MmapTileRecastConfig
{
	float walkableSlopeAngle;
	unsigned char walkableRadius;
	unsigned char walkableHeight;
	unsigned char walkableClimb;
	unsigned char padding0;
	uint32 vertexPerMapEdge;
	uint32 vertexPerTileEdge;
	uint32 tilesPerMapEdge;
	float baseUnitDim;
	float cellSizeHorizontal;
	float cellSizeVertical;
	float maxSimplificationError;
};

// The 56-byte header in front of every MMMXXYY.mmtile. TrinityCore's header is only the
// first 20 bytes of this, which is why a TrinityCore reader starts 36 bytes early.
struct MmapTileHeader
{
	uint32 mmapMagic;
	uint32 dtVersion;
	uint32 mmapVersion;
	uint32 size;          // byte length of the Detour blob that follows
	char usesLiquids;
	char padding[3];
	MmapTileRecastConfig recastConfig;
};

static_assert(sizeof(MmapTileRecastConfig) == 36, "MmapTileRecastConfig must match AzerothCore's 36 bytes");
static_assert(sizeof(MmapTileHeader) == 56, "MmapTileHeader must match AzerothCore's 56 bytes");

enum TileLoadResult
{
	TILE_LOADED,  // read, validated and added to the navmesh
	TILE_MISSING, // no such file; normal at map edges
	TILE_INVALID, // file exists but was refused; the reason has been logged as an error
};

/// Reads MMM.mmap (exactly one dtNavMeshParams). Logs an error and returns false on any problem.
bool readNavMeshParams(rcContext* ctx, const char* path, dtNavMeshParams& params);

/// Validates a tile file's header and blob layout without touching a navmesh.
/// On TILE_LOADED, data is dtAlloc'd (caller frees it or hands it to addTile) and header is filled.
TileLoadResult readTile(rcContext* ctx, const char* path, MmapTileHeader& header,
						unsigned char*& data, int& dataSize);

/// readTile + dtNavMesh::addTile. On TILE_LOADED the navmesh owns the data.
/// If tileRef is not null it receives the added tile's ref.
TileLoadResult addTileFromFile(rcContext* ctx, dtNavMesh* mesh, const char* path, dtTileRef* tileRef = 0);

/// The byte size dtNavMesh::addTile assumes for a blob with these counts (DetourNavMesh.cpp).
int expectedBlobSize(const dtMeshHeader& header);

/// The generator's debugOutput mesh name, "<dir>/mapMMMXXYY.obj". XX and YY are in the same
/// order as in the tile's file name MMMXXYY.mmtile.
struct DebugMeshName
{
	std::string dir;   // directory holding the .obj, and the .mmap/.mmtile files beside it
	int mapId;
	int mapIdDigits;   // AzerothCore zero-pads to 3
	int x;
	int y;
};

/// Returns false if path's file name is not map<digits>XXYY.obj.
bool parseDebugMeshName(const std::string& path, DebugMeshName& out);
std::string mmapPath(const DebugMeshName& name);
std::string tilePath(const DebugMeshName& name, int x, int y);

/// Reads <dir>/MMM.mmap, then adds every tile within `radius` of (name.x, name.y).
/// Missing tiles are skipped. Any tile that exists but is refused fails the whole load (logged),
/// so a format mismatch can never look like an empty map. Returns null on failure.
dtNavMesh* loadTileNeighbourhood(rcContext* ctx, const DebugMeshName& name, int radius, int* loadedTiles);

}

#endif // MMAPTILE_H
