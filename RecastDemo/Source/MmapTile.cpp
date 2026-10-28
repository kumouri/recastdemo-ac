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

#include "MmapTile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "DetourAlloc.h"
#include "DetourCommon.h"
#include "Recast.h"

namespace mmap
{

namespace
{
// Length of an open file, or -1.
long fileLength(FILE* fp)
{
	if (fseek(fp, 0, SEEK_END) != 0)
		return -1;
	const long len = ftell(fp);
	if (fseek(fp, 0, SEEK_SET) != 0)
		return -1;
	return len;
}
}

int expectedBlobSize(const dtMeshHeader& header)
{
	return dtAlign4(sizeof(dtMeshHeader))
		+ dtAlign4(sizeof(float) * 3 * header.vertCount)
		+ dtAlign4(sizeof(dtPoly) * header.polyCount)
		+ dtAlign4(sizeof(dtLink) * header.maxLinkCount)
		+ dtAlign4(sizeof(dtPolyDetail) * header.detailMeshCount)
		+ dtAlign4(sizeof(float) * 3 * header.detailVertCount)
		+ dtAlign4(sizeof(unsigned char) * 4 * header.detailTriCount)
		+ dtAlign4(sizeof(dtBVNode) * header.bvNodeCount)
		+ dtAlign4(sizeof(dtOffMeshConnection) * header.offMeshConCount);
}

bool readNavMeshParams(rcContext* ctx, const char* path, dtNavMeshParams& params)
{
	FILE* fp = fopen(path, "rb");
	if (!fp)
	{
		ctx->log(RC_LOG_ERROR, "mmap: cannot open '%s'.", path);
		return false;
	}
	const long len = fileLength(fp);
	const size_t count = len == (long)sizeof(dtNavMeshParams) ? fread(&params, sizeof(params), 1, fp) : 0;
	fclose(fp);
	if (len != (long)sizeof(dtNavMeshParams) || count != 1)
	{
		ctx->log(RC_LOG_ERROR, "mmap: '%s' is %ld bytes; an AzerothCore .mmap is exactly %d (dtNavMeshParams).",
				 path, len, (int)sizeof(dtNavMeshParams));
		return false;
	}
	return true;
}

TileLoadResult readTile(rcContext* ctx, const char* path, MmapTileHeader& header,
						unsigned char*& data, int& dataSize)
{
	data = 0;
	dataSize = 0;
	FILE* fp = fopen(path, "rb");
	if (!fp)
		return TILE_MISSING;

	const long len = fileLength(fp);
	memset(&header, 0, sizeof(header));
	// Read the 20 bytes every mmtile flavour shares before trusting the rest of the header,
	// so a TrinityCore or older AzerothCore file gets a version error, not a size error.
	const size_t prefix = 5 * sizeof(uint32);
	if (len < (long)prefix || fread(&header, prefix, 1, fp) != 1)
	{
		fclose(fp);
		ctx->log(RC_LOG_ERROR, "mmtile: '%s' is %ld bytes, too short for a tile header.", path, len);
		return TILE_INVALID;
	}
	if (header.mmapMagic != MMAP_MAGIC)
	{
		fclose(fp);
		ctx->log(RC_LOG_ERROR, "mmtile: '%s' has magic 0x%08x, not 'MMAP' 0x%08x.", path, header.mmapMagic, MMAP_MAGIC);
		return TILE_INVALID;
	}
	if (header.mmapVersion != MMAP_VERSION)
	{
		fclose(fp);
		ctx->log(RC_LOG_ERROR, "mmtile: '%s' is MMAP version %u. This build reads only AzerothCore v%u tiles "
				 "(56-byte header); TrinityCore and older tiles use a different header.",
				 path, header.mmapVersion, MMAP_VERSION);
		return TILE_INVALID;
	}
	if (header.dtVersion != (uint32)DT_NAVMESH_VERSION)
	{
		fclose(fp);
		ctx->log(RC_LOG_ERROR, "mmtile: '%s' was built with Detour navmesh version %u, this Detour is %d.",
				 path, header.dtVersion, DT_NAVMESH_VERSION);
		return TILE_INVALID;
	}
	if (fread(((char*)&header) + prefix, sizeof(header) - prefix, 1, fp) != 1 ||
		len != (long)sizeof(header) + (long)header.size)
	{
		fclose(fp);
		ctx->log(RC_LOG_ERROR, "mmtile: '%s' is %ld bytes but its header says %d + %u.",
				 path, len, (int)sizeof(header), header.size);
		return TILE_INVALID;
	}
	if (header.size < sizeof(dtMeshHeader))
	{
		fclose(fp);
		ctx->log(RC_LOG_ERROR, "mmtile: '%s' blob is %u bytes, smaller than dtMeshHeader.", path, header.size);
		return TILE_INVALID;
	}

	unsigned char* blob = (unsigned char*)dtAlloc(header.size, DT_ALLOC_PERM);
	if (!blob)
	{
		fclose(fp);
		ctx->log(RC_LOG_ERROR, "mmtile: out of memory reading '%s' (%u bytes).", path, header.size);
		return TILE_INVALID;
	}
	const size_t got = fread(blob, header.size, 1, fp);
	fclose(fp);
	if (got != 1)
	{
		dtFree(blob);
		ctx->log(RC_LOG_ERROR, "mmtile: short read on '%s'.", path);
		return TILE_INVALID;
	}

	const dtMeshHeader* mesh = (const dtMeshHeader*)blob;
	if (mesh->magic != DT_NAVMESH_MAGIC || mesh->version != DT_NAVMESH_VERSION)
	{
		dtFree(blob);
		ctx->log(RC_LOG_ERROR, "mmtile: '%s' blob has magic 0x%08x version %d, not a Detour v%d tile.",
				 path, mesh->magic, mesh->version, DT_NAVMESH_VERSION);
		return TILE_INVALID;
	}
	// addTile never checks the size, so a polyref-width mismatch would mis-slice silently.
	const int expected = expectedBlobSize(*mesh);
	if (expected != (int)header.size)
	{
		dtFree(blob);
		ctx->log(RC_LOG_ERROR, "mmtile: '%s' blob is %u bytes but its counts need %d with sizeof(dtLink)=%d. "
				 "The tile and this Detour disagree on DT_POLYREF64.",
				 path, header.size, expected, (int)sizeof(dtLink));
		return TILE_INVALID;
	}

	data = blob;
	dataSize = (int)header.size;
	return TILE_LOADED;
}

TileLoadResult addTileFromFile(rcContext* ctx, dtNavMesh* mesh, const char* path, dtTileRef* tileRef)
{
	MmapTileHeader header;
	unsigned char* data = 0;
	int dataSize = 0;
	const TileLoadResult result = readTile(ctx, path, header, data, dataSize);
	if (result != TILE_LOADED)
		return result;

	dtTileRef ref = 0;
	const dtStatus status = mesh->addTile(data, dataSize, DT_TILE_FREE_DATA, 0, &ref);
	if (dtStatusFailed(status))
	{
		dtFree(data);
		ctx->log(RC_LOG_ERROR, "mmtile: dtNavMesh::addTile refused '%s' (status 0x%08x).", path, status);
		return TILE_INVALID;
	}
	if (tileRef)
		*tileRef = ref;
	return TILE_LOADED;
}

bool parseDebugMeshName(const std::string& path, DebugMeshName& out)
{
	const size_t slash = path.find_last_of("/\\");
	const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
	const size_t n = file.size();
	// "map" + at least one map id digit + XXYY + ".obj"
	if (n < 3 + 1 + 4 + 4 || file.compare(0, 3, "map") != 0 || file.compare(n - 4, 4, ".obj") != 0)
		return false;
	const std::string digits = file.substr(3, n - 3 - 4);
	if (digits.size() > 3 + 4 + 2)
		return false;
	for (size_t i = 0; i < digits.size(); ++i)
		if (digits[i] < '0' || digits[i] > '9')
			return false;
	out.dir = slash == std::string::npos ? std::string(".") : path.substr(0, slash);
	out.mapIdDigits = (int)digits.size() - 4;
	out.mapId = atoi(digits.substr(0, out.mapIdDigits).c_str());
	out.x = atoi(digits.substr(out.mapIdDigits, 2).c_str());
	out.y = atoi(digits.substr(out.mapIdDigits + 2, 2).c_str());
	return true;
}

std::string mmapPath(const DebugMeshName& name)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "/%0*d.mmap", name.mapIdDigits, name.mapId);
	return name.dir + buf;
}

std::string tilePath(const DebugMeshName& name, int x, int y)
{
	char buf[48];
	snprintf(buf, sizeof(buf), "/%0*d%02d%02d.mmtile", name.mapIdDigits, name.mapId, x, y);
	return name.dir + buf;
}

dtNavMesh* loadTileNeighbourhood(rcContext* ctx, const DebugMeshName& name, int radius, int* loadedTiles)
{
	if (loadedTiles)
		*loadedTiles = 0;
	dtNavMeshParams params;
	if (!readNavMeshParams(ctx, mmapPath(name).c_str(), params))
		return 0;
	dtNavMesh* mesh = dtAllocNavMesh();
	if (!mesh || dtStatusFailed(mesh->init(&params)))
	{
		ctx->log(RC_LOG_ERROR, "mmap: dtNavMesh::init rejected '%s'.", mmapPath(name).c_str());
		dtFreeNavMesh(mesh);
		return 0;
	}
	int loaded = 0;
	for (int dx = -radius; dx <= radius; ++dx)
	{
		for (int dy = -radius; dy <= radius; ++dy)
		{
			const std::string path = tilePath(name, name.x + dx, name.y + dy);
			const TileLoadResult r = addTileFromFile(ctx, mesh, path.c_str());
			if (r == TILE_INVALID)
			{
				dtFreeNavMesh(mesh);
				return 0;
			}
			if (r == TILE_LOADED)
				++loaded;
		}
	}
	if (!loaded)
	{
		ctx->log(RC_LOG_ERROR, "mmtile: no tiles found around '%s'.", tilePath(name, name.x, name.y).c_str());
		dtFreeNavMesh(mesh);
		return 0;
	}
	ctx->log(RC_LOG_PROGRESS, "mmtile: loaded %d tile(s) around map %d tile %02d%02d.", loaded, name.mapId, name.x, name.y);
	if (loadedTiles)
		*loadedTiles = loaded;
	return mesh;
}

}
