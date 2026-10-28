//
// mmtile_loadcheck: headless proof that RecastDemo's mmtile reader loads a whole
// AzerothCore map. Uses exactly the code behind "Load mmtile" (MmapTile.cpp), but
// takes every MMMXXYY.mmtile in a directory instead of a 3x3 neighbourhood.
//
// usage: mmtile_loadcheck <mmaps dir> <map id> [--expect-tiles N] [--expect-polys N]
//                         [--expect-cross-links N] [--path wowX wowY wowZ wowX wowY wowZ]
//
// Exit 0: loaded and every expectation met. 1: an expectation failed. 2: a file was refused
// (the reason is printed) or the arguments were wrong.
//

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "DetourCommon.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "MmapTile.h"
#include "Recast.h"

namespace
{
class PrintContext : public rcContext
{
public:
	PrintContext() : rcContext(true) {}
protected:
	void doLog(const rcLogCategory category, const char* msg, const int len) override
	{
		std::printf("%s%.*s\n", category == RC_LOG_ERROR ? "REFUSED: " : "", len, msg);
	}
};

bool isTileName(const std::string& file, int mapId)
{
	char prefix[8];
	std::snprintf(prefix, sizeof(prefix), "%03d", mapId);
	if (file.size() != 3 + 4 + 7 || file.compare(0, 3, prefix) != 0 || file.compare(7, 7, ".mmtile") != 0)
		return false;
	for (int i = 3; i < 7; ++i)
		if (file[i] < '0' || file[i] > '9')
			return false;
	return true;
}

int fail(const char* what, long got, long want)
{
	std::printf("FAIL: %s is %ld, expected %ld\n", what, got, want);
	return 1;
}
}

int main(int argc, char** argv)
{
	if (argc < 3)
	{
		std::printf("usage: %s <mmaps dir> <map id> [--expect-tiles N] [--expect-polys N] [--expect-cross-links N]\n"
			"       [--path wowX wowY wowZ wowX wowY wowZ]\n", argv[0]);
		return 2;
	}
	const std::filesystem::path dir = argv[1];
	const int mapId = std::atoi(argv[2]);
	long expectTiles = -1, expectPolys = -1, expectCross = -1;
	bool wantPath = false;
	float wow[6] = {0, 0, 0, 0, 0, 0};
	for (int i = 3; i < argc; ++i)
	{
		if (!std::strcmp(argv[i], "--expect-tiles") && i + 1 < argc)
			expectTiles = std::atol(argv[++i]);
		else if (!std::strcmp(argv[i], "--expect-polys") && i + 1 < argc)
			expectPolys = std::atol(argv[++i]);
		else if (!std::strcmp(argv[i], "--expect-cross-links") && i + 1 < argc)
			expectCross = std::atol(argv[++i]);
		else if (!std::strcmp(argv[i], "--path") && i + 6 < argc)
		{
			wantPath = true;
			for (int k = 0; k < 6; ++k)
				wow[k] = static_cast<float>(std::atof(argv[++i]));
		}
		else
		{
			std::printf("unrecognised argument '%s'\n", argv[i]);
			return 2;
		}
	}

	std::printf("Detour: DT_NAVMESH_VERSION=%d sizeof(dtPolyRef)=%d sizeof(dtLink)=%d MmapTileHeader=%d bytes (MMAP v%u)\n",
		DT_NAVMESH_VERSION, (int)sizeof(dtPolyRef), (int)sizeof(dtLink), (int)sizeof(mmap::MmapTileHeader), mmap::MMAP_VERSION);

	PrintContext ctx;
	char mmapName[16];
	std::snprintf(mmapName, sizeof(mmapName), "%03d.mmap", mapId);
	dtNavMeshParams params;
	if (!mmap::readNavMeshParams(&ctx, (dir / mmapName).string().c_str(), params))
		return 2;
	dtNavMesh* mesh = dtAllocNavMesh();
	if (!mesh || dtStatusFailed(mesh->init(&params)))
	{
		std::printf("REFUSED: dtNavMesh::init rejected %s\n", mmapName);
		return 2;
	}

	std::vector<std::string> files;
	for (const auto& entry : std::filesystem::directory_iterator(dir))
		if (entry.is_regular_file() && isTileName(entry.path().filename().string(), mapId))
			files.push_back(entry.path().string());
	for (const std::string& f : files)
	{
		if (mmap::addTileFromFile(&ctx, mesh, f.c_str()) != mmap::TILE_LOADED)
		{
			dtFreeNavMesh(mesh);
			return 2;
		}
	}

	long tiles = 0, polys = 0, verts = 0, links = 0, cross = 0, offMesh = 0;
	const dtNavMesh* cmesh = mesh;
	for (int i = 0; i < cmesh->getMaxTiles(); ++i)
	{
		const dtMeshTile* tile = cmesh->getTile(i);
		if (!tile || !tile->header)
			continue;
		++tiles;
		polys += tile->header->polyCount;
		verts += tile->header->vertCount;
		offMesh += tile->header->offMeshConCount;
		for (int p = 0; p < tile->header->polyCount; ++p)
			for (unsigned int l = tile->polys[p].firstLink; l != DT_NULL_LINK; l = tile->links[l].next)
			{
				++links;
				if (tile->links[l].side != 0xff)
					++cross;
			}
	}
	std::printf("map %03d: files=%d tiles=%ld (maxTiles=%d) polys=%ld verts=%ld links=%ld crossTileLinks=%ld offMeshCons=%ld\n",
		mapId, (int)files.size(), tiles, params.maxTiles, polys, verts, links, cross, offMesh);

	int rc = 0;
	if (expectTiles >= 0 && tiles != expectTiles)
		rc = fail("tiles", tiles, expectTiles);
	if (expectPolys >= 0 && polys != expectPolys)
		rc = fail("polys", polys, expectPolys);
	if (expectCross >= 0 && cross != expectCross)
		rc = fail("crossTileLinks", cross, expectCross);

	if (wantPath)
	{
		// Recast (x, y, z) = WoW (Y, Z, X).
		const float a[3] = {wow[1], wow[2], wow[0]};
		const float b[3] = {wow[4], wow[5], wow[3]};
		const float ext[3] = {5.0f, 10.0f, 5.0f};
		dtNavMeshQuery* q = dtAllocNavMeshQuery();
		dtQueryFilter filter;
		dtPolyRef ra = 0, rb = 0, corridor[4096];
		float pa[3], pb[3], straight[3 * 1024];
		int n = 0, ns = 0;
		q->init(mesh, 4096);
		q->findNearestPoly(a, ext, &filter, &ra, pa);
		q->findNearestPoly(b, ext, &filter, &rb, pb);
		if (!ra || !rb || dtStatusFailed(q->findPath(ra, rb, pa, pb, &filter, corridor, &n, 4096)) || !n)
		{
			std::printf("path: none\n");
			rc = 1;
		}
		else
		{
			q->findStraightPath(pa, pb, corridor, n, straight, 0, 0, &ns, 1024);
			float len = 0;
			for (int i = 1; i < ns; ++i)
				len += dtVdist(&straight[(i - 1) * 3], &straight[i * 3]);
			std::printf("path: %d polys, %d corners, %.1f yards, reached=%s\n", n, ns, len,
				corridor[n - 1] == rb ? "yes" : "no");
		}
		dtFreeNavMeshQuery(q);
	}

	dtFreeNavMesh(mesh);
	std::printf(rc ? "FAIL\n" : "OK\n");
	return rc;
}
