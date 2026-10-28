#include "catch.hpp"

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "DetourAlloc.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "MmapTile.h"
#include "Recast.h"

namespace
{
// Keeps the last error so tests can check that a refusal says why.
class CaptureContext : public rcContext
{
public:
	CaptureContext() : rcContext(true) {}
	std::string lastError;
protected:
	virtual void doLog(const rcLogCategory category, const char* msg, const int len)
	{
		if (category == RC_LOG_ERROR)
			lastError.assign(msg, len);
	}
};

// One 10x10 quad, built by Detour itself, so the blob layout is whatever this Detour writes.
std::vector<unsigned char> buildBlob()
{
	const unsigned short verts[] = {0, 0, 0, 0, 0, 10, 10, 0, 10, 10, 0, 0};
	const unsigned short polys[] = {0, 1, 2, 3, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff};
	const unsigned short flags[] = {1};
	const unsigned char areas[] = {1};
	dtNavMeshCreateParams p;
	memset(&p, 0, sizeof(p));
	p.verts = verts;
	p.vertCount = 4;
	p.polys = polys;
	p.polyFlags = flags;
	p.polyAreas = areas;
	p.polyCount = 1;
	p.nvp = 6;
	p.walkableHeight = 2;
	p.walkableRadius = 0.5f;
	p.walkableClimb = 0.5f;
	p.bmax[0] = 10;
	p.bmax[1] = 1;
	p.bmax[2] = 10;
	p.cs = 1;
	p.ch = 1;
	p.buildBvTree = true;
	unsigned char* data = 0;
	int size = 0;
	REQUIRE(dtCreateNavMeshData(&p, &data, &size));
	std::vector<unsigned char> blob(data, data + size);
	dtFree(data);
	return blob;
}

mmap::MmapTileHeader v20Header(unsigned int size)
{
	mmap::MmapTileHeader h;
	memset(&h, 0, sizeof(h));
	h.mmapMagic = mmap::MMAP_MAGIC;
	h.dtVersion = DT_NAVMESH_VERSION;
	h.mmapVersion = mmap::MMAP_VERSION;
	h.size = size;
	h.usesLiquids = 1;
	h.recastConfig.walkableSlopeAngle = 60.0f;
	h.recastConfig.walkableClimb = 6;
	return h;
}

const char* const kPath = "mmaptile_test.mmtile";

void writeFile(const void* a, size_t na, const void* b = 0, size_t nb = 0)
{
	FILE* fp = fopen(kPath, "wb");
	REQUIRE(fp);
	fwrite(a, 1, na, fp);
	if (b)
		fwrite(b, 1, nb, fp);
	fclose(fp);
}

mmap::TileLoadResult read(CaptureContext& ctx, mmap::MmapTileHeader& h)
{
	unsigned char* data = 0;
	int size = 0;
	const mmap::TileLoadResult r = mmap::readTile(&ctx, kPath, h, data, size);
	if (r == mmap::TILE_LOADED)
	{
		REQUIRE(data);
		REQUIRE(size == (int)h.size);
		dtFree(data);
	}
	else
	{
		REQUIRE(!data);
	}
	return r;
}
}

TEST_CASE("MmapTile reads AzerothCore v20 tiles and refuses everything else loudly")
{
	const std::vector<unsigned char> blob = buildBlob();
	CaptureContext ctx;
	mmap::MmapTileHeader got;

	SECTION("a v20 tile loads, and addTile accepts it")
	{
		const mmap::MmapTileHeader h = v20Header((unsigned int)blob.size());
		writeFile(&h, sizeof(h), &blob[0], blob.size());
		REQUIRE(read(ctx, got) == mmap::TILE_LOADED);
		REQUIRE(got.recastConfig.walkableClimb == 6);
		REQUIRE(got.recastConfig.walkableSlopeAngle == 60.0f);

		dtNavMeshParams params;
		memset(&params, 0, sizeof(params));
		params.tileWidth = 10;
		params.tileHeight = 10;
		params.maxTiles = 1;
		params.maxPolys = 1;
		dtNavMesh* mesh = dtAllocNavMesh();
		REQUIRE(dtStatusSucceed(mesh->init(&params)));
		dtTileRef ref = 0;
		REQUIRE(mmap::addTileFromFile(&ctx, mesh, kPath, &ref) == mmap::TILE_LOADED);
		REQUIRE(ref != 0);
		const dtNavMesh* cmesh = mesh;
		REQUIRE(cmesh->getTileByRef(ref)->header->polyCount == 1);
		dtFreeNavMesh(mesh);
	}

	SECTION("a TrinityCore tile (20-byte header) is refused by version, not skipped")
	{
		mmap::MmapTileHeader h = v20Header((unsigned int)blob.size());
		h.mmapVersion = 15;
		writeFile(&h, 20, &blob[0], blob.size());
		REQUIRE(read(ctx, got) == mmap::TILE_INVALID);
		REQUIRE(ctx.lastError.find("MMAP version 15") != std::string::npos);
	}

	SECTION("an older AzerothCore version is refused")
	{
		mmap::MmapTileHeader h = v20Header((unsigned int)blob.size());
		h.mmapVersion = 19;
		writeFile(&h, sizeof(h), &blob[0], blob.size());
		REQUIRE(read(ctx, got) == mmap::TILE_INVALID);
		REQUIRE(ctx.lastError.find("MMAP version 19") != std::string::npos);
	}

	SECTION("a v20 header in front of a 20-byte-header payload is refused by size")
	{
		const mmap::MmapTileHeader h = v20Header((unsigned int)blob.size());
		writeFile(&h, 20, &blob[0], blob.size());
		REQUIRE(read(ctx, got) == mmap::TILE_INVALID);
		REQUIRE(ctx.lastError.find("header says") != std::string::npos);
	}

	SECTION("wrong magic is refused")
	{
		mmap::MmapTileHeader h = v20Header((unsigned int)blob.size());
		h.mmapMagic = 0x12345678;
		writeFile(&h, sizeof(h), &blob[0], blob.size());
		REQUIRE(read(ctx, got) == mmap::TILE_INVALID);
		REQUIRE(ctx.lastError.find("magic") != std::string::npos);
	}

	SECTION("a blob whose counts don't match its size (polyref width mismatch) is refused")
	{
		std::vector<unsigned char> bad = blob;
		((dtMeshHeader*)&bad[0])->maxLinkCount += 1;
		const mmap::MmapTileHeader h = v20Header((unsigned int)bad.size());
		writeFile(&h, sizeof(h), &bad[0], bad.size());
		REQUIRE(read(ctx, got) == mmap::TILE_INVALID);
		REQUIRE(ctx.lastError.find("DT_POLYREF64") != std::string::npos);
	}

	SECTION("a missing file is reported as missing, without an error")
	{
		remove(kPath);
		REQUIRE(read(ctx, got) == mmap::TILE_MISSING);
		REQUIRE(ctx.lastError.empty());
	}

	remove(kPath);
}

TEST_CASE("MmapTile reads .mmap params and refuses wrong sizes")
{
	CaptureContext ctx;
	dtNavMeshParams params;
	memset(&params, 0, sizeof(params));
	params.maxTiles = 42;
	writeFile(&params, sizeof(params));
	dtNavMeshParams got;
	REQUIRE(mmap::readNavMeshParams(&ctx, kPath, got));
	REQUIRE(got.maxTiles == 42);

	writeFile(&params, sizeof(params) - 4);
	REQUIRE(!mmap::readNavMeshParams(&ctx, kPath, got));
	REQUIRE(ctx.lastError.find("exactly 28") != std::string::npos);
	remove(kPath);
}

TEST_CASE("MmapTile parses the generator's debug mesh names")
{
	mmap::DebugMeshName n;
	REQUIRE(mmap::parseDebugMeshName("Meshes/map6092742.obj", n));
	REQUIRE(n.dir == "Meshes");
	REQUIRE(n.mapId == 609);
	REQUIRE(n.mapIdDigits == 3);
	REQUIRE(n.x == 27);
	REQUIRE(n.y == 42);
	REQUIRE(mmap::mmapPath(n) == "Meshes/609.mmap");
	REQUIRE(mmap::tilePath(n, 26, 43) == "Meshes/6092643.mmtile");

	REQUIRE(mmap::parseDebugMeshName("C:\\data\\meshes\\map0003132.obj", n));
	REQUIRE(n.mapId == 0);
	REQUIRE(mmap::tilePath(n, 31, 32) == "C:\\data\\meshes/0003132.mmtile");

	REQUIRE(!mmap::parseDebugMeshName("Meshes/dungeon.obj", n));
	REQUIRE(!mmap::parseDebugMeshName("Meshes/map6092742.mesh", n));
	REQUIRE(!mmap::parseDebugMeshName("Meshes/map27x42.obj", n));
}
