/*
 *
 *  Iter Vehemens ad Necem (IVAN)
 *  Copyright (C) Timo Kiviluoto
 *  Released under the GNU General
 *  Public License
 *
 *  See LICENSING which should be included
 *  along with this file for more details
 *
 */

/* The whole `--wildernesstest` diagnostic: the biome/placement/lifecycle
   checks, the abort and save-resume subprocess entry points, and the isolated
   environment they run in. It is a guarded translation unit of its own so that
   the game's main.cpp does not carry three thousand lines of test machinery;
   with -DIVAN_WILDERNESS_TEST=OFF it compiles to nothing. */
#ifdef WILDERNESS_TEST_HARNESS

#include <iostream>
#include <cstdlib>

#include "game.h"
#include "database.h"
#include "feio.h"
#include "igraph.h"
#include "iconf.h"
#include "whandler.h"
#include "hscore.h"
#include "graphics.h"
#include "script.h"
#include "message.h"
#include "proto.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "level.h"
#include "dungeon.h"
#include "char.h"
#include "lsquare.h"
#include "stack.h"
#include "save.h"
#include "team.h"
#include "human.h"
#include "nonhuman.h"
#include "lterras.h"
#include "materias.h"
#include "miscitem.h"
#include "room.h"
#include "confdef.h"
#include "iconf.h"
#include "worldmap.h"
#include "wsquare.h"
#include "wterras.h"
#include "pool.h"
#include "command.h"

/* Absolute path of this executable, resolved by Main() before the diagnostic
   moves into its temporary directory, so the negative tests can re-exec it. */
static char WildernessTestSelf[PATH_MAX];
/* The disposable directory every file this run writes is kept in. */
static char WildernessTestTmp[PATH_MAX];
/* Where the process came from, so the isolated environment can be undone
   before the tree is deleted. */
static char WildernessTestOldCwd[PATH_MAX];
static festring WildernessTestOldHome;

namespace
{
  struct testbiome
  {
    int Dungeon;
    int Type;
    const char* Name;
  };

  const testbiome TestBiomes[] =
  {
    { WILDERNESS_JUNGLE,           JUNGLE,           "jungle" },
    { WILDERNESS_LEAFY_FOREST,     LEAFY_FOREST,     "leafyforest" },
    { WILDERNESS_EVERGREEN_FOREST, EVERGREEN_FOREST, "evergreenforest" },
    { WILDERNESS_STEPPE,           STEPPE,           "steppe" },
    { WILDERNESS_DESERT,           DESERT,           "desert" },
    { WILDERNESS_TUNDRA,           TUNDRA,           "tundra" },
    { WILDERNESS_GLACIER,          GLACIER,          "glacier" },
    { WILDERNESS_OCEAN,            OCEAN_LEVEL,      "ocean" }
  };

  const int TestBiomeCount = sizeof(TestBiomes) / sizeof(testbiome);

  /* Wildlife the generators are allowed to produce, one table per biome. */
  struct testallow
  {
    const char* ClassID;
    const char* Adjective;
  };

  const testallow JungleAllowed[] =
  {
    { "snake", 0 }, { "spider", "large" }, { "carnivorousplant", 0 },
    { "largerat", 0 }, { "spider", "giant" }
  };

  const testallow LeafyAllowed[] =
  {
    { "hedgehog", 0 }, { "skunk", 0 }, { "largerat", 0 }, { "wolf", 0 },
    { "bear", "black" }, { "magpie", 0 }
  };

  const testallow EvergreenAllowed[] =
  {
    { "wolf", 0 }, { "hedgehog", 0 }, { "bear", "black" },
    { "bear", "grizzly" }, { "twoheadedmoose", 0 }, { "magpie", 0 }
  };

  const testallow SteppeAllowed[] =
  {
    { "jackal", 0 }, { "snake", 0 }, { "wolf", 0 }, { "buffalo", 0 },
    { "lion", 0 }
  };

  const testallow DesertAllowed[] =
  {
    { "jackal", 0 }, { "snake", 0 }, { "spider", "large" }
  };

  const testallow TundraAllowed[] =
  {
    { "wolf", 0 }, { "mammoth", 0 }, { "twoheadedmoose", 0 },
    { "bear", "polar" }
  };

  const testallow GlacierAllowed[] =
  {
    { "bear", "polar" }
  };

  const testallow OceanAllowed[] =
  {
    { "dolphin", 0 }
  };

  const testallow* GetAllowedTable(int Type, int& Size)
  {
    switch(Type)
    {
     case JUNGLE:
      Size = sizeof(JungleAllowed) / sizeof(testallow);
      return JungleAllowed;
     case LEAFY_FOREST:
      Size = sizeof(LeafyAllowed) / sizeof(testallow);
      return LeafyAllowed;
     case EVERGREEN_FOREST:
      Size = sizeof(EvergreenAllowed) / sizeof(testallow);
      return EvergreenAllowed;
     case STEPPE:
      Size = sizeof(SteppeAllowed) / sizeof(testallow);
      return SteppeAllowed;
     case DESERT:
      Size = sizeof(DesertAllowed) / sizeof(testallow);
      return DesertAllowed;
     case TUNDRA:
      Size = sizeof(TundraAllowed) / sizeof(testallow);
      return TundraAllowed;
     case GLACIER:
      Size = sizeof(GlacierAllowed) / sizeof(testallow);
      return GlacierAllowed;
     case OCEAN_LEVEL:
      Size = sizeof(OceanAllowed) / sizeof(testallow);
      return OceanAllowed;
    }

    Size = 0;
    return 0;
  }

  bool IsAllowed(const testallow* Table, int Size,
		 const char* ClassID, const festring& Adjective)
  {
    for(int c = 0; c < Size; ++c)
      if(!strcmp(Table[c].ClassID, ClassID)
	 && (!Table[c].Adjective || Adjective == Table[c].Adjective))
	return true;

    return false;
  }

  /* Biomes that can carry plants at all; a glacier is bare ice and an ocean is
     open water, so demanding vegetation from either would be a false alarm. */
  bool HasVegetation(int Type)
  {
    return Type != GLACIER && Type != OCEAN_LEVEL;
  }

  /* Biomes whose generator always produces plants; the rest are stochastic and
     may legitimately come up empty in a single short batch. */
  bool GuaranteedVegetation(int Type)
  {
    switch(Type)
    {
     case JUNGLE:
     case LEAFY_FOREST:
     case EVERGREEN_FOREST:
     case DESERT:
      return true;
    }

    return false;
  }

  bool GuaranteedObstacles(int Type)
  {
    switch(Type)
    {
     case JUNGLE:
     case LEAFY_FOREST:
     case EVERGREEN_FOREST:
     case TUNDRA:
     case GLACIER:
      return true;
    }

    return false;
  }

  /* The decoration configurations each biome's generator may place. Anything
     else on a wilderness map means the terrain classification drifted. */
  bool IsBiomeTreeConfig(int Type, int Config)
  {
    switch(Type)
    {
     case JUNGLE:
      return Config == PALM || Config == TEAK || Config == JUNGLE_CANOPY
	|| Config == JUNGLE_THICKET;
     case LEAFY_FOREST:
      return Config == OAK || Config == BIRCH;
     case EVERGREEN_FOREST:
      return Config == PINE || Config == FIR;
     case STEPPE:
      return Config == OAK || Config == BIRCH;
     case DESERT:
      return Config == CACTUS || Config == PALM;
     case TUNDRA:
      return Config == DWARF_BIRCH;
    }

    return false;
  }

  bool IsDecorationTerrain(const olterrain* O)
  {
    return O && !strcmp(O->GetProtoType()->GetClassID(), "decoration");
  }

  /* S3.6: every slot the diagnostic asks for must be inside the container and
     must never be the reserved world-map sentinel. */
  int TestSlot(int S)
  {
    int Slot = 1 + (S % (WILDERNESS_LEVEL_SLOTS - 1));

    if(Slot == WORLD_MAP)
      Slot = WORLD_MAP + 1;

    return Slot;
  }

  bool ReachesMapEdge(level* L, v2 From)
  {
    int XSize = L->GetXSize();
    int YSize = L->GetYSize();
    std::vector<char> Seen(XSize * YSize, 0);
    std::vector<v2> Stack;
    ulong Head = 0;
    Seen[From.Y * XSize + From.X] = 1;
    Stack.push_back(From);

    while(Head < Stack.size())
    {
      v2 Pos = Stack[Head++];

      if(Pos.X == 0 || Pos.Y == 0 || Pos.X == XSize - 1 || Pos.Y == YSize - 1)
	return true;

      for(int d = 0; d < 8; ++d)
      {
	v2 Next = Pos + game::GetMoveVector(d);

	if(Next.X < 0 || Next.Y < 0 || Next.X >= XSize || Next.Y >= YSize)
	  continue;

	int I = Next.Y * XSize + Next.X;

	if(Seen[I] || !(L->GetLSquare(Next)->GetWalkability() & WALK))
	  continue;

	Seen[I] = 1;
	Stack.push_back(Next);
      }
    }

    return false;
  }

  /* E3/S3.2: the exit guarantee is movement, not geometry. The four routes are
     no longer required to be a literal straight three-wide cross; what matters
     is that a four-square creature can actually reach every edge. */
  int CorridorFailures(level* L)
  {
    int XSize = L->GetXSize();
    int YSize = L->GetYSize();
    int CenterX = XSize / 2;
    int CenterY = YSize / 2;
    v2 Center(CenterX, CenterY);
    v2 Edges[4] = { v2(0, CenterY), v2(XSize - 1, CenterY),
		    v2(CenterX, 0), v2(CenterX, YSize - 1) };
    int Failures = 0;

    for(int c = 0; c < 4; ++c)
      if(!L->WildernessWideRouteExists(Center, Edges[c]))
	++Failures;

    return Failures;
  }

  int CountWater(level* L)
  {
    int Water = 0;

    for(int x = 0; x < L->GetXSize(); ++x)
      for(int y = 0; y < L->GetYSize(); ++y)
      {
	int Walk = L->GetLSquare(x, y)->GetWalkability();

	if((Walk & SWIM) && !(Walk & WALK))
	  ++Water;
      }

    return Water;
  }

  /* A square a member can physically be put on, ignoring movement mode: the
     source may legitimately hold a swim-only follower. */
  /* Makes a water square beside the given position. A swim-only companion
     needs one to stand on, and the biome generators are stochastic about
     pools, so the diagnostic supplies its own rather than relying on them. */
  void EnsureWaterNear(level* L, v2 Near)
  {
    for(int d = 0; d < 8; ++d)
    {
      v2 Pos = Near + game::GetMoveVector(d);

      if(L->IsValidPos(Pos) && !L->GetLSquare(Pos)->GetCharacter())
      {
	L->GetLSquare(Pos)->ChangeOLTerrain(0);
	L->GetLSquare(Pos)->ChangeGLTerrain(liquidterrain::Spawn(POOL));
	return;
      }
    }
  }

  /* Ground fluid squares and rain-list totals, for the weather checks: a spell
     must not spill standing liquid, and repeated state changes must not grow
     the per-square rain lists. */
  int CountGroundFluids(level* L)
  {
    int Count = 0;

    for(int x = 0; x < L->GetXSize(); ++x)
      for(int y = 0; y < L->GetYSize(); ++y)
	if(L->GetLSquare(x, y)->HasGroundFluidForTest())
	  ++Count;

    return Count;
  }

  void CountRains(level* L, int& Total, int& Enabled)
  {
    Total = 0;
    Enabled = 0;

    for(int x = 0; x < L->GetXSize(); ++x)
      for(int y = 0; y < L->GetYSize(); ++y)
      {
	int T = 0, E = 0;
	L->GetLSquare(x, y)->CountRainsForTest(T, E);
	Total += T;
	Enabled += E;
      }
  }

  /* The nearest square the character itself may occupy. For a multi-square
     creature that means its whole footprint, not just the anchor square, which
     is why the character and not a bare level is asked. */
  bool FindFreeSquare(level* L, const character* C, v2 Near, v2& Result)
  {
    if(L->IsValidPos(Near) && C->CanMoveOn(L->GetLSquare(Near))
       && C->IsFreeForMe(L->GetLSquare(Near)))
    {
      Result = Near;
      return true;
    }

    for(int Radius = 1; Radius < L->GetXSize(); ++Radius)
      for(int dx = -Radius; dx <= Radius; ++dx)
	for(int dy = -Radius; dy <= Radius; ++dy)
	{
	  if(abs(dx) != Radius && abs(dy) != Radius)
	    continue;

	  v2 Pos = Near + v2(dx, dy);

	  if(L->IsValidPos(Pos) && C->CanMoveOn(L->GetLSquare(Pos))
	     && C->IsFreeForMe(L->GetLSquare(Pos)))
	  {
	    Result = Pos;
	    return true;
	  }
	}

    return false;
  }

  /* Per-biome tallies accumulated over a whole seed batch. */
  struct biomecount
  {
    biomecount(int XSize, int YSize)
      : Creatures(0), Vegetation(0), Obstacles(0), Water(0), BadTreeConfig(0),
	BadCreatures(0), EmptyVegetationSeeds(0), EmptyObstacleSeeds(0),
	ShallowWaterSeeds(0), Thickets(0), WalkableThickets(0),
	FlyBlockedThickets(0), EtherealBlockedThickets(0),
	UndestroyableThickets(0), MinCoverage(1e9), MaxCoverage(0),
	MinNear(1e9), MaxNear(0), MinOpaque(1e9), MaxOpaque(0),
	MinOpen(1e9), MaxOpen(0), MinIceGround(1e9), MaxIceGround(0),
	VegCells((XSize / 8 + 1) * (YSize / 8 + 1), 0),
	ObstCells((XSize / 8 + 1) * (YSize / 8 + 1), 0)
    { }

    int Creatures;
    int Vegetation;
    int Obstacles;
    int Water;
    int BadTreeConfig;
    int BadCreatures;
    int EmptyVegetationSeeds;
    int EmptyObstacleSeeds;
    int ShallowWaterSeeds;
    /* Impassable jungle understory: counted to prove the thicket really blocks
       walking while still letting flight and ethereal movement through. */
    int Thickets;
    int WalkableThickets;
    int FlyBlockedThickets;
    int EtherealBlockedThickets;
    int UndestroyableThickets;
    /* E6 spatial metrics: the per-seed range of decoration coverage and of the
       fraction of cells in or next to vegetation. */
    double MinCoverage;
    double MaxCoverage;
    double MinNear;
    double MaxNear;
    /* F4 metrics: opaque line-of-sight cover and the largest single open
       region, so a biome cannot pass merely on aggregate counts. */
    double MinOpaque;
    double MaxOpaque;
    double MinOpen;
    double MaxOpen;
    /* Fraction of the map whose ground is exposed glacier ice rather than
       snow, so a glacier cannot pass as snow studded with wall islands. */
    double MinIceGround;
    double MaxIceGround;
    std::vector<char> VegCells;
    std::vector<char> ObstCells;

    int Spread(const std::vector<char>& Cells) const
    {
      return int(std::count(Cells.begin(), Cells.end(), char(1)));
    }
  };

  /* Walks one generated level and tallies everything the acceptance criteria
     name. The adjective is read from the character's own configuration, never
     from an array offset (S6). */
  void TallyBiome(level* L, int Type, const testallow* Allowed, int AllowedSize,
		  const char* BiomeName, biomecount& Count, int Seed)
  {
    int XSize = L->GetXSize();
    int YSize = L->GetYSize();
    int Veg = 0;
    int Obst = 0;
    int Water = 0;

    for(int x = 0; x < XSize; ++x)
      for(int y = 0; y < YSize; ++y)
      {
	lsquare* Square = L->GetLSquare(x, y);
	int Walk = Square->GetWalkability();
	olterrain* O = Square->GetOLTerrain();
	int Cell = (y / 8) * (XSize / 8 + 1) + (x / 8);

	if(IsDecorationTerrain(O))
	{
	  ++Veg;
	  ++Count.Vegetation;
	  Count.VegCells[Cell] = 1;

	  if(O->GetConfig() == JUNGLE_THICKET)
	  {
	    ++Count.Thickets;

	    if(Walk & WALK)
	      ++Count.WalkableThickets;

	    if(!(Walk & FLY))
	      ++Count.FlyBlockedThickets;

	    if(!(Walk & ETHEREAL))
	      ++Count.EtherealBlockedThickets;

	    if(!O->CanBeDestroyed())
	      ++Count.UndestroyableThickets;
	  }

	  if(!IsBiomeTreeConfig(Type, O->GetConfig()))
	  {
	    if(Count.BadTreeConfig < 5)
	      std::cout << "  BAD " << BiomeName << " seed " << Seed
			<< ": decoration config " << O->GetConfig()
			<< " does not belong to this biome at " << x << "," << y
			<< std::endl;

	    ++Count.BadTreeConfig;
	  }
	}
	else if(O)
	{
	  ++Obst;
	  ++Count.Obstacles;
	  Count.ObstCells[Cell] = 1;
	}

	if((Walk & SWIM) && !(Walk & WALK))
	{
	  ++Water;
	  ++Count.Water;
	}

	character* C = Square->GetCharacter();

	if(!C)
	  continue;

	++Count.Creatures;
	const characterprototype* Proto =
	  protocontainer<character>::GetProto(C->GetType());
	const char* ClassID = Proto->GetClassID();
	const festring& Adjective = C->GetAdjective();

	if(!IsAllowed(Allowed, AllowedSize, ClassID, Adjective))
	{
	  if(Count.BadCreatures < 10)
	    std::cout << "  BAD " << BiomeName << " seed " << Seed << ": "
		      << ClassID << " [" << Adjective.CStr() << "] at "
		      << x << "," << y << std::endl;

	  ++Count.BadCreatures;
	}
      }

    if(HasVegetation(Type) && Veg == 0)
      ++Count.EmptyVegetationSeeds;

    if(Type != OCEAN_LEVEL && Obst == 0)
      ++Count.EmptyObstacleSeeds;

    /* T4: checked per seed, not on the batch total, so a generator that makes
       land on most seeds cannot hide behind one wet one over a long run. */
    if(Type == OCEAN_LEVEL && Water < XSize * YSize / 2)
      ++Count.ShallowWaterSeeds;

    /* E6: broad spatial metrics. Coverage is the decoration fraction of this
       seed; near-coverage is the fraction of cells holding vegetation or
       standing next to it, which distinguishes a connected forest matrix from
       a few isolated groves. */
    int Near = 0;

    for(int x = 0; x < XSize; ++x)
      for(int y = 0; y < YSize; ++y)
      {
	truth Has = IsDecorationTerrain(L->GetLSquare(x, y)->GetOLTerrain());

	for(int d = 0; d < 8 && !Has; ++d)
	{
	  v2 P = v2(x, y) + game::GetMoveVector(d);

	  if(L->IsValidPos(P)
	     && IsDecorationTerrain(L->GetLSquare(P)->GetOLTerrain()))
	    Has = true;
	}

	if(Has)
	  ++Near;
      }

    double Coverage = double(Veg) / (XSize * YSize);
    double NearFraction = double(Near) / (XSize * YSize);

    if(Coverage < Count.MinCoverage) Count.MinCoverage = Coverage;
    if(Coverage > Count.MaxCoverage) Count.MaxCoverage = Coverage;
    if(NearFraction < Count.MinNear) Count.MinNear = NearFraction;
    if(NearFraction > Count.MaxNear) Count.MaxNear = NearFraction;

    /* F4: opaque LOS cover and the largest single open region. A dense-looking
       but fully transparent jungle fails the first; a biome that is one giant
       empty field fails the second. */
    int Opaque = 0;
    int IceGround = 0;
    std::vector<char> Open(XSize * YSize, 0);

    for(int x = 0; x < XSize; ++x)
      for(int y = 0; y < YSize; ++y)
      {
	lsquare* Square = L->GetLSquare(x, y);
	olterrain* O = Square->GetOLTerrain();

	if(O && !O->IsTransparent())
	  ++Opaque;

	if(Square->GetGLTerrain()
	   && Square->GetGLTerrain()->GetConfig() == GLACIER_ICE)
	  ++IceGround;

	if(!O && (Square->GetWalkability() & WALK))
	  Open[y * XSize + x] = 1;
      }

    std::vector<char> SeenOpen(XSize * YSize, 0);
    int LargestOpen = 0;

    for(int x = 0; x < XSize; ++x)
      for(int y = 0; y < YSize; ++y)
      {
	int Start = y * XSize + x;

	if(!Open[Start] || SeenOpen[Start])
	  continue;

	std::vector<int> Stack;
	Stack.push_back(Start);
	SeenOpen[Start] = 1;
	int Size = 0;

	while(!Stack.empty())
	{
	  int I = Stack.back();
	  Stack.pop_back();
	  ++Size;
	  int X = I % XSize;
	  int Y = I / XSize;

	  for(int d = 0; d < 4; ++d)
	  {
	    v2 P = v2(X, Y) + game::GetBasicMoveVector(d);

	    if(!L->IsValidPos(P))
	      continue;

	    int J = P.Y * XSize + P.X;

	    if(Open[J] && !SeenOpen[J])
	    {
	      SeenOpen[J] = 1;
	      Stack.push_back(J);
	    }
	  }
	}

	if(Size > LargestOpen)
	  LargestOpen = Size;
      }

    double OpaqueFraction = double(Opaque) / (XSize * YSize);
    double OpenFraction = double(LargestOpen) / (XSize * YSize);
    double IceFraction = double(IceGround) / (XSize * YSize);

    if(OpaqueFraction < Count.MinOpaque) Count.MinOpaque = OpaqueFraction;
    if(OpaqueFraction > Count.MaxOpaque) Count.MaxOpaque = OpaqueFraction;
    if(OpenFraction < Count.MinOpen) Count.MinOpen = OpenFraction;
    if(OpenFraction > Count.MaxOpen) Count.MaxOpen = OpenFraction;
    if(IceFraction < Count.MinIceGround) Count.MinIceGround = IceFraction;
    if(IceFraction > Count.MaxIceGround) Count.MaxIceGround = IceFraction;
  }

  /* E6: broad stochastic bands for vegetation coverage, in percent of the map.
     They guard the intended structure (dense forest, sparse steppe) without
     demanding an exact map. Glacier and ocean carry no trees by design. */
  void BiomeCoverageBand(int Type, double& Min, double& Max)
  {
    switch(Type)
    {
     case JUNGLE: Min = 13; Max = 35; break;
     case LEAFY_FOREST: Min = 8; Max = 27; break;
     case EVERGREEN_FOREST: Min = 11; Max = 30; break;
     case DESERT: Min = 0.02; Max = 4; break;
     case STEPPE: Min = 0; Max = 3; break;
     case TUNDRA: Min = 0; Max = 4; break;
     default: Min = 0; Max = 0; break;
    }
  }

  /* Reads back the identity and position of everything standing on a level, so
     a reload can be compared occupant by occupant. */
  void CollectOccupants(level* L, std::vector<std::pair<ulong, v2> >& Out)
  {
    Out.clear();

    for(int x = 0; x < L->GetXSize(); ++x)
      for(int y = 0; y < L->GetYSize(); ++y)
      {
	character* C = L->GetLSquare(x, y)->GetCharacter();

	if(C)
	  Out.push_back(std::make_pair(C->GetID(), v2(x, y)));
      }

    std::sort(Out.begin(), Out.end());
  }

  /* Empties a level so a hand-built map is not disturbed by the wildlife the
     generator left on it. */  void ClearCreatures(level* L)
  {
    for(int x = 0; x < L->GetXSize(); ++x)
      for(int y = 0; y < L->GetYSize(); ++y)
	if(character* C = L->GetLSquare(x, y)->GetCharacter())
	{
	  C->Remove();
	  delete C;
	}
  }

  /* True when a square anywhere on the level holds the item with this id, so a
     reload can be asked whether a dropped item actually came back. */
  bool LevelHasItem(level* L, ulong ID)
  {
    for(int x = 0; x < L->GetXSize(); ++x)
      for(int y = 0; y < L->GetYSize(); ++y)
      {
	stack* St = L->GetLSquare(x, y)->GetStack();

	for(int i = 0; i < St->GetItems(); ++i)
	  if(St->GetItem(i)->GetID() == ID)
	    return true;
      }

    return false;
  }

  /* T1: an enabled character standing somewhere ticks and counts for team
     checks on every frame, so it must not be left behind on an area that is no
     longer active. */
  int PlacedEnabledCount()
  {
    int Count = 0;

    for(int t = 0; t < game::GetTeams(); ++t)
    {
      const std::list<character*>& Members = game::GetTeam(t)->GetMember();

      for(std::list<character*>::const_iterator i = Members.begin();
	  i != Members.end(); ++i)
	if((*i)->IsEnabled() && (*i)->GetSquareUnder())
	  ++Count;
    }

    return Count;
  }

  /* S1/S2 acceptance: every member of a completed transfer must stand on a
     square it can actually move on and that is free for its own footprint. */
  int PlacementFailures(character* C, const char* Label)
  {
    if(!C)
    {
      std::cout << "  FAIL " << Label << ": missing character" << std::endl;
      return 1;
    }

    level* L = game::GetCurrentLevel();

    if(!C->GetSquareUnder() || !L || !L->IsValidPos(C->GetPos()))
    {
      std::cout << "  FAIL " << Label << ": not placed" << std::endl;
      return 1;
    }

    lsquare* S = L->GetLSquare(C->GetPos());

    if(S->GetCharacter() != C || !C->CanMoveOn(S) || !C->IsFreeForMe(S))
    {
      std::cout << "  FAIL " << Label << ": stands at " << C->GetPos().X
		<< "," << C->GetPos().Y << " which is occupied or unusable"
		<< std::endl;
      return 1;
    }

    return 0;
  }

  /* T4: GetPos() is only meaningful once a character is on a map, so reporting
     a failed placement must not dereference it. A diagnostic that crashes while
     describing the failure hides the failure. */
  festring SafePos(const character* C)
  {
    festring Result;

    if(!C)
      return CONST_S("missing");

    if(!C->GetSquareUnder())
      return CONST_S("off the map");

    Result << C->GetPos().X << ',' << C->GetPos().Y;
    return Result;
  }

  /* The file a level is kept in, without going through the dungeon's own
     current save name. */
  festring LevelFilePath(dungeon* D, int Slot)
  {
    return D->GetLevelFileName(game::SaveName(), Slot);
  }

  /* Releases every loaded level except the one in use. An ordinary pool tick
     then sees only the active area, which is what "no off-screen simulation"
     means; the levels are reloaded from disk the next time they are entered.
     The reserved world-map slot is skipped for the wilderness containers,
     which reject it. */
  void UnloadInactiveLevels(int KeepDungeon, int KeepSlot)
  {
    const std::map<int, dungeonscript>& Script =
      game::GetGameScript()->GetDungeon();

    for(std::map<int, dungeonscript>::const_iterator i = Script.begin();
	i != Script.end(); ++i)
    {
      dungeon* D = game::GetDungeon(i->first);
      truth Wild = game::IsWildernessDungeon(i->first);
      int Levels = D->GetLevels();

      for(int s = 0; s < Levels; ++s)
      {
	if(Wild && s == WORLD_MAP)
	  continue;

	if(i->first == KeepDungeon && s == KeepSlot)
	  continue;

	if(D->GetLevel(s))
	  D->UnloadLevel(s);
      }
    }
  }

  /* Prepares a fresh local level as the area TryTravel() would leave. */
  level* SetupLocalSource(int DungeonIndex, int Slot)
  {
    game::SetIsInWilderness(false);
    game::SetCurrentDungeonIndex(DungeonIndex);
    game::SetCurrentLevelIndex(Slot);

    dungeon* D = game::GetDungeon(DungeonIndex);
    game::SetIsGenerating(true);
    D->PrepareLevel(Slot, false);
    game::SetIsGenerating(false);

    return D->GetLevel(Slot);
  }

  /* CurrentArea, not the level argument, is what PutTo(v2) resolves through. */
  void PlaceCharacter(character* C, level* L, v2 Pos)
  {
    if(C->GetSquareUnder())
      C->Remove();

    L->GetLSquare(Pos)->KickAnyoneStandingHereAway();
    C->PutTo(Pos);
  }

  /* Spawns a named creature on the player's team with the follow flag set, so
     CollectCreatures() will offer it to the transfer. */
  character* AddFollower(const char* ClassName, level* L, v2 Near)
  {
    int Type = protocontainer<character>::SearchCodeName(ClassName);

    if(!Type)
      return 0;

    const characterprototype* Proto = protocontainer<character>::GetProto(Type);
    const characterdatabase*const* Data = Proto->GetConfigData();
    int Config = -1;
    int Fallback = -1;

    for(int c = 0; c < Proto->GetConfigSize(); ++c)
    {
      if(Data[c]->IsAbstract)
	continue;

      if(!Data[c]->Adjective.GetSize())
      {
	Config = Data[c]->Config;
	break;
      }

      if(Fallback < 0)
	Fallback = Data[c]->Config;
    }

    if(Config < 0)
      Config = Fallback;

    if(Config < 0)
      return 0;

    character* C = Proto->Spawn(Config);
    C->SetTeam(game::GetTeam(PLAYER_TEAM));
    C->SetCommandFlags(C->GetCommandFlags() | FOLLOW_LEADER);

    v2 Pos;

    if(!FindFreeSquare(L, C, Near, Pos))
    {
      delete C;
      return 0;
    }

    C->PutTo(Pos);
    return C;
  }

  /* CollectCreatures() follows only when no enabled hostile team stands in the
     way; the diagnostic takes the monsters out of the way deliberately so the
     placement logic, not line of sight, decides the outcome. Both directions
     are restored afterwards. */
  struct relationguard
  {
    relationguard()
    {
      Player = game::GetTeam(PLAYER_TEAM);
      Monster = game::GetTeam(MONSTER_TEAM);
      PlayerToMonster = Player->GetRelation(Monster);
      MonsterToPlayer = Monster->GetRelation(Player);
      Player->SetRelation(Monster, UNCARING);
      Monster->SetRelation(Player, UNCARING);
    }

    ~relationguard()
    {
      Player->SetRelation(Monster, PlayerToMonster);
      Monster->SetRelation(Player, MonsterToPlayer);
    }

    team* Player;
    team* Monster;
    int PlayerToMonster;
    int MonsterToPlayer;
  };

  /* Re-executes this binary with a case that has to terminate through
     globalerrorhandler::Abort(), i.e. exit status 4. The control case runs the
     same bootstrap and must not abort, which is what makes an abort in the
     other cases attributable to the check itself. */
  int RunAbortCase(const char* Case, const char* Label, int Expected)
  {
    if(!WildernessTestSelf[0])
    {
      std::cout << "  FAIL " << Label << ": no self path for re-exec" << std::endl;
      return 1;
    }

    pid_t Child = fork();

    if(Child < 0)
    {
      std::cout << "  FAIL " << Label << ": fork failed" << std::endl;
      return 1;
    }

    if(!Child)
    {
      int Null = open("/dev/null", O_WRONLY);

      if(Null >= 0)
      {
	dup2(Null, 1);
	dup2(Null, 2);
	close(Null);
      }

      execl(WildernessTestSelf, WildernessTestSelf, "--wildernessabort",
	    Case, static_cast<char*>(0));
      _exit(127);
    }

    int Status = 0;

    if(waitpid(Child, &Status, 0) < 0)
    {
      std::cout << "  FAIL " << Label << ": waitpid failed" << std::endl;
      return 1;
    }

    int Code = WIFEXITED(Status) ? WEXITSTATUS(Status) : -1;

    if(Code != Expected)
    {
      std::cout << "  FAIL " << Label << ": expected exit " << Expected
		<< ", got " << Code << std::endl;
      return 1;
    }

    return 0;
  }

  /* Runs one save-or-resume phase in a fresh process. Production Load() builds
     its arrays and maps from the file and has no teardown path, so resuming
     over live game state would leave the old game alive; a separate process is
     what a real "Continue Game" gets and is the only honest way to exercise
     the same code. The child inherits this process's isolated directory and
     prints its own failures. */
  int RunResumePhase(const char* Phase, const char* Name)
  {
    if(!WildernessTestSelf[0])
    {
      std::cout << "  FAIL resume " << Phase << " " << Name
		<< ": no self path for re-exec" << std::endl;
      return 1;
    }

    pid_t Child = fork();

    if(Child < 0)
    {
      std::cout << "  FAIL resume " << Phase << " " << Name
		<< ": fork failed" << std::endl;
      return 1;
    }

    if(!Child)
    {
      execl(WildernessTestSelf, WildernessTestSelf, "--wildernessresume",
	    Phase, Name, static_cast<char*>(0));
      _exit(127);
    }

    int Status = 0;

    if(waitpid(Child, &Status, 0) < 0)
    {
      std::cout << "  FAIL resume " << Phase << " " << Name
		<< ": waitpid failed" << std::endl;
      return 1;
    }

    int Code = WIFEXITED(Status) ? WEXITSTATUS(Status) : -1;

    if(Code != 0)
    {
      std::cout << "  FAIL resume " << Phase << " " << Name
		<< ": child exited " << Code << std::endl;
      return 1;
    }

    return 0;
  }

  void RemoveTree(const char* Path)
  {
    DIR* Dir = opendir(Path);

    if(!Dir)
      return;

    std::string Prefix = std::string(Path) + "/";
    struct dirent* Entry;

    while((Entry = readdir(Dir)))
    {
      if(!strcmp(Entry->d_name, ".") || !strcmp(Entry->d_name, ".."))
	continue;

      std::string Child = Prefix + Entry->d_name;
      struct stat St;

      if(!lstat(Child.c_str(), &St) && S_ISDIR(St.st_mode))
	RemoveTree(Child.c_str());
      else
	unlink(Child.c_str());
    }

    closedir(Dir);
    rmdir(Path);
  }

  /* T5: the disposable environment has to exist before ivanconfig::Initialize()
     and the rest of startup, or they read and write the ordinary configuration
     and save directory first and the isolation is only retrofitted. Called from
     Main() while argv[0] is still meaningful; fails closed, because a run that
     cannot be isolated is not run at all. */
  bool SetUpWildernessIsolation(const char* Self)
  {
    if(!realpath(Self, WildernessTestSelf))
    {
      strncpy(WildernessTestSelf, Self, sizeof(WildernessTestSelf) - 1);
      WildernessTestSelf[sizeof(WildernessTestSelf) - 1] = 0;
    }

    char TmpDir[] = "/tmp/ivan-wildtest-XXXXXX";

    if(!mkdtemp(TmpDir))
    {
      std::cout << "FAIL mkdtemp: " << strerror(errno) << std::endl;
      return false;
    }

    strncpy(WildernessTestTmp, TmpDir, sizeof(WildernessTestTmp) - 1);
    WildernessTestTmp[sizeof(WildernessTestTmp) - 1] = 0;

    festring SaveRoot;
    SaveRoot << TmpDir << "/IvanSave";

    if(mkdir(SaveRoot.CStr(), 0700) && errno != EEXIST)
    {
      std::cout << "FAIL cannot create " << SaveRoot.CStr() << ": "
		<< strerror(errno) << std::endl;
      RemoveTree(TmpDir);
      WildernessTestTmp[0] = 0;
      return false;
    }

    if(!getcwd(WildernessTestOldCwd, sizeof(WildernessTestOldCwd)))
      WildernessTestOldCwd[0] = 0;

    const char* Home = getenv("HOME");
    WildernessTestOldHome = Home ? Home : "";

    if(chdir(TmpDir) || setenv("HOME", TmpDir, 1))
    {
      std::cout << "FAIL could not isolate working directory/HOME: "
		<< strerror(errno) << std::endl;

      /* Undo what did work and take the tree down again; do not continue with
	 a half-owned environment. */
      if(WildernessTestOldCwd[0])
	chdir(WildernessTestOldCwd);

      if(WildernessTestOldHome.GetSize())
	setenv("HOME", WildernessTestOldHome.CStr(), 1);
      else
	unsetenv("HOME");

      RemoveTree(TmpDir);
      WildernessTestTmp[0] = 0;
      return false;
    }

    /* Autosave output would otherwise land in the real save directory: both the
       file name and the save root are redirected into the temporary tree. */
    game::SetAutoSaveFileNameForTest(SaveRoot + "/AutoSave");
    ivanconfig::SetAutoSaveIntervalForTest(1000);

    return true;
  }

  /* The globals game::Init() creates before a game can run, in the same order:
     the danger map is derived from the player, so the player has to exist
     first. Shared by the in-process checks and the subprocess phases. */
  void BootstrapGame()
  {
    game::InitScript();
    game::CreateTeams();
    game::CreateGods();
    game::SetIsInWilderness(true);
    game::SetPlayer(playerkind::Spawn());
    game::GetPlayer()->SetTeam(game::GetTeam(PLAYER_TEAM));
    game::GetTeam(PLAYER_TEAM)->SetLeader(game::GetPlayer());
    game::InitDangerMap();
    game::InitDungeons();
  }

  /* Enabled characters that are standing somewhere other than the active
     area. After a clean resume there must be none: a live off-screen map would
     keep ticking and counting for team checks. */
  int InactivePlacedCount()
  {
    int Count = 0;
    level* Active = game::GetCurrentLevel();

    for(int t = 0; t < game::GetTeams(); ++t)
    {
      const std::list<character*>& Members = game::GetTeam(t)->GetMember();

      for(std::list<character*>::const_iterator i = Members.begin();
	  i != Members.end(); ++i)
	if((*i)->IsEnabled() && (*i)->GetSquareUnder()
	   && (!Active || (*i)->GetLevel() != Active))
	  ++Count;
    }

    return Count;
  }

  /* Every enabled character with an id, for a global-uniqueness check. */
  int DuplicateCharacterIDs()
  {
    std::map<ulong, int> Seen;
    int Duplicates = 0;

    for(int t = 0; t < game::GetTeams(); ++t)
    {
      const std::list<character*>& Members = game::GetTeam(t)->GetMember();

      for(std::list<character*>::const_iterator i = Members.begin();
	  i != Members.end(); ++i)
	if(++Seen[(*i)->GetID()] > 1)
	  ++Duplicates;
    }

    return Duplicates;
  }
}

/* Child entry point for the cases that must abort. Each returns normally only
   if the check it exercises failed to fire. */
int WildernessAbortTest(const char* Case)
{
  if(!Case)
    return 1;

  game::InitScript();
  game::InitDungeons();

  if(!strcmp(Case, "clean"))
  {
    dungeon* D = game::GetDungeon(WILDERNESS_JUNGLE);

    if(!D)
      return 1;

    /* Its own biome must pass, which is what makes the mismatch below an
       attributable failure rather than a check that always fires. */
    D->TestValidateWildernessBiome(JUNGLE);
    D->TestValidateWildernessLevelProfile(D->GetLevelScript(0));
    return 0;
  }

  if(!strcmp(Case, "slot"))
  {
    /* The reserved world-map index is not a level of a wilderness container. */
    game::GetDungeon(WILDERNESS_JUNGLE)->IsGenerated(WORLD_MAP);
    return 1;
  }

  if(!strcmp(Case, "capacity"))
  {
    /* S5: a container whose capacity does not match the world is refused by
       the same startup check, with the same abort. */
    game::GetDungeon(WILDERNESS_JUNGLE)
      ->TestValidateWildernessCapacity(WILDERNESS_LEVEL_SLOTS - 1);
    return 1;
  }

  if(!strcmp(Case, "biome"))
  {
    /* Another biome's profile must be refused, not quietly generated. */
    game::GetDungeon(WILDERNESS_JUNGLE)
      ->TestValidateWildernessBiome(LEAFY_FOREST);
    return 1;
  }

  if(!strcmp(Case, "levelprofile"))
  {
    /* A per-level override that is not even a wilderness profile must be
       refused before any map is built or loaded. */
    game::GetDungeon(WILDERNESS_JUNGLE)
      ->TestValidateWildernessLevelProfile(
	game::GetDungeon(ELPURI_CAVE)->GetLevelScript(0));
    return 1;
  }

  return 1;
}

/* Subprocess entry point for the save-and-resume checks. Production Load()
   builds its arrays and maps from the file instead of tearing a previous game
   down, so resuming in the same process as a live game would keep the old maps
   and entities alive and duplicate their ids. Each phase therefore runs in a
   fresh process, the way the "Continue Game" menu does, and the resumed state
   is compared against facts captured by the save phase rather than against
   live pointers. */
int WildernessResumeCase(const char* Phase, const char* Name)
{
  if(!Phase || !Name)
    return 1;

  festring Base = !strcmp(Name, "AutoSave")
    ? game::GetAutoSaveFileName() : game::SaveName(Name);
  festring FactsPath = festring(Base) + ".facts";

  if(!strcmp(Phase, "save"))
  {
    BootstrapGame();
    /* Level files are named after PlayerName; the resume uses Name, so the
       save has to as well or the destination file would be looked for under a
       different name. */
    game::SetPlayerNameForTest(Name);

    /* A concrete, resumable slice of the game: a generated wilderness area
       with the player standing on it, a mutated statistic and a dropped
       item. */
    const int Slot = TestSlot(1700);
    level* L = SetupLocalSource(WILDERNESS_JUNGLE, Slot);
    /* Stand on the level's own arrival square, because that is where the
       resume and any later re-entry put the player; a position the entry does
       not use would make the leave/re-enter comparison meaningless. */
    v2 Pos = L->GetEntryPos(0, WILDERNESS_LOCAL_ENTRY);
    character* P = game::GetPlayer();
    PlaceCharacter(P, L, Pos);
    P->SetMoney(123456);
    item* Dropped = banana::Spawn();
    L->GetLSquare(v2(1, 1))->AddItem(Dropped);
    ulong DroppedID = Dropped->GetID();

    /* Give the saved map its biome weather and pin a mid-spell phase, so the
       resume has to bring back the exact spell rather than roll a fresh one. */
    game::InitializeLevelEnvironment();
    game::SetGlobalRainLiquid(L->GetGlobalRainLiquid());
    game::SetGlobalRainSpeed(L->GetGlobalRainSpeed());
    L->ForceWeatherForTest(WEATHER_HEAVY, 777);
    int WeatherState = L->GetWeatherState();
    long WeatherTimer = L->GetWeatherTimer();

    std::vector<std::pair<ulong, v2> > Occupants;
    CollectOccupants(L, Occupants);

    game::Save(Base);

    FILE* Facts = fopen(FactsPath.CStr(), "w");

    if(!Facts)
    {
      std::cout << "  FAIL could not write " << FactsPath.CStr() << std::endl;
      return 1;
    }

    fprintf(Facts, "%d %d %d %d %d %lu %d %d %ld\n",
	    game::GetCurrentDungeonIndex(), game::GetCurrentLevelIndex(),
	    game::IsInWilderness() ? 1 : 0, int(Occupants.size()),
	    int(P->GetMoney()), DroppedID, PlacedEnabledCount(),
	    WeatherState, WeatherTimer);

    for(uint c = 0; c < Occupants.size(); ++c)
      fprintf(Facts, "%lu %d %d\n", Occupants[c].first,
	      Occupants[c].second.X, Occupants[c].second.Y);

    fclose(Facts);
    return 0;
  }

  if(!strcmp(Phase, "load"))
  {
    FILE* Facts = fopen(FactsPath.CStr(), "r");

    if(!Facts)
    {
      std::cout << "  FAIL no facts file " << FactsPath.CStr() << std::endl;
      return 1;
    }

    int Dungeon = 0, Level = 0, Wild = 0, Count = 0, Money = 0, Placed = 0;
    int WeatherState = -1;
    long WeatherTimer = -1;
    unsigned long ItemID = 0;

    if(fscanf(Facts, "%d %d %d %d %d %lu %d %d %ld",
	      &Dungeon, &Level, &Wild, &Count, &Money, &ItemID, &Placed,
	      &WeatherState, &WeatherTimer) != 9)
    {
      std::cout << "  FAIL malformed facts file" << std::endl;
      fclose(Facts);
      return 1;
    }

    std::vector<std::pair<ulong, v2> > Expected;

    for(int c = 0; c < Count; ++c)
    {
      unsigned long ID;
      int X, Y;

      if(fscanf(Facts, "%lu %d %d", &ID, &X, &Y) != 3)
      {
	std::cout << "  FAIL malformed facts occupants" << std::endl;
	fclose(Facts);
	return 1;
      }

      Expected.push_back(std::make_pair(ID, v2(X, Y)));
    }

    fclose(Facts);
    std::sort(Expected.begin(), Expected.end());

    int Failures = 0;

    /* Resume through the very call the "Continue Game" menu uses. */
    if(!game::Init(Name))
    {
      std::cout << "  FAIL Continue Game could not resume " << Name
		<< std::endl;
      return 1;
    }

    if(PlacedEnabledCount() != Placed)
    {
      std::cout << "  FAIL resumed game has " << PlacedEnabledCount()
		<< " placed creatures, expected " << Placed << std::endl;
      ++Failures;
    }

    if(DuplicateCharacterIDs())
    {
      std::cout << "  FAIL resumed game has duplicate character ids"
		<< std::endl;
      ++Failures;
    }

    if(InactivePlacedCount())
    {
      std::cout << "  FAIL resumed game has " << InactivePlacedCount()
		<< " enabled creature(s) off the active area" << std::endl;
      ++Failures;
    }

    if(game::GetCurrentDungeonIndex() != Dungeon
       || game::GetCurrentLevelIndex() != Level
       || game::IsInWilderness() != (Wild != 0))
    {
      std::cout << "  FAIL resumed game is in the wrong area" << std::endl;
      ++Failures;
    }
    else
    {
      std::vector<std::pair<ulong, v2> > Actual;
      CollectOccupants(game::GetCurrentLevel(), Actual);

      if(Actual != Expected)
      {
	std::cout << "  FAIL resumed population differs from the saved one"
		  << std::endl;
	++Failures;
      }

      if(!LevelHasItem(game::GetCurrentLevel(), ItemID))
      {
	std::cout << "  FAIL the dropped item did not survive the resume"
		  << std::endl;
	++Failures;
      }
    }

    if(!game::GetPlayer() || game::GetPlayer()->GetMoney() != Money)
    {
      std::cout << "  FAIL the resumed player lost a mutation" << std::endl;
      ++Failures;
    }

    /* The weather cycle is level-owned and serialized: the resume must land in
       the exact spell, with the material present and the binding installed. */
    if(!game::GetCurrentLevel()->HasWeather()
       || game::GetCurrentLevel()->GetWeatherState() != WeatherState
       || game::GetCurrentLevel()->GetWeatherTimer() != WeatherTimer
       || !game::GetCurrentLevel()->GetGlobalRainLiquid()
       || game::GetGlobalRainLiquid()
	  != game::GetCurrentLevel()->GetGlobalRainLiquid())
    {
      std::cout << "  FAIL the resumed weather phase changed" << std::endl;
      ++Failures;
    }

    /* An ordinary pool tick has to be safe with only the resumed game live. */
    pool::Be();
    pool::BurnHell();

    if(InactivePlacedCount())
    {
      std::cout << "  FAIL a pool tick revived an off-screen area" << std::endl;
      ++Failures;
    }

    /* Leaving and re-entering the saved area makes it inactive in between and
       has to bring back exactly the resumed population. */
    if(!Failures)
    {
      if(!game::TryTravel(WILDERNESS_LEAFY_FOREST, TestSlot(1710),
			  WILDERNESS_LOCAL_ENTRY, false, false))
      {
	std::cout << "  FAIL could not leave the resumed area" << std::endl;
	++Failures;
      }
      else if(!game::TryTravel(Dungeon, Level, WILDERNESS_LOCAL_ENTRY,
			       false, false))
      {
	std::cout << "  FAIL could not re-enter the resumed area" << std::endl;
	++Failures;
      }
      else
      {
	std::vector<std::pair<ulong, v2> > Again;
	CollectOccupants(game::GetCurrentLevel(), Again);

	if(Again != Expected)
	{
	  std::cout << "  FAIL the re-entered area differs from the resume"
		    << std::endl;
	  ++Failures;
	}
      }
    }

    return Failures ? 1 : 0;
  }

  if(!strcmp(Phase, "saveworld"))
  {
    BootstrapGame();
    game::SetPlayerNameForTest(Name);

    /* A save made while standing on the world map goes through the other half
       of game::Load() (the .wm file rather than a level file). */
    worldmap* Sea = new worldmap(WORLD_MAP_WIDTH, WORLD_MAP_HEIGHT);
    game::SetWorldMapForTest(Sea);
    game::SetIsInWilderness(true);
    game::SetCurrentArea(Sea);
    game::SetCurrentWSquareMap(Sea->GetMap());
    game::SetCurrentLevel(0);
    game::SetCurrentLSquareMap(0);
    game::SetCurrentDungeonIndex(WORLD_MAP);
    game::SetCurrentLevelIndex(WORLD_MAP);

    character* P = game::GetPlayer();

    if(P->GetSquareUnder())
      P->Remove();

    P->PutTo(v2(10, 10));
    P->SetMoney(654321);
    game::Save(Base);

    FILE* Facts = fopen(FactsPath.CStr(), "w");

    if(!Facts)
    {
      std::cout << "  FAIL could not write " << FactsPath.CStr() << std::endl;
      return 1;
    }

    fprintf(Facts, "%d %d %d\n", int(P->GetMoney()), P->GetPos().X,
	    P->GetPos().Y);
    fclose(Facts);
    return 0;
  }

  if(!strcmp(Phase, "loadworld"))
  {
    FILE* Facts = fopen(FactsPath.CStr(), "r");

    if(!Facts)
    {
      std::cout << "  FAIL no facts file " << FactsPath.CStr() << std::endl;
      return 1;
    }

    int Money = 0, X = 0, Y = 0;

    if(fscanf(Facts, "%d %d %d", &Money, &X, &Y) != 3)
    {
      std::cout << "  FAIL malformed world facts file" << std::endl;
      fclose(Facts);
      return 1;
    }

    fclose(Facts);
    int Failures = 0;

    if(!game::Init(Name))
    {
      std::cout << "  FAIL Continue Game could not resume world " << Name
		<< std::endl;
      return 1;
    }

    if(!game::IsInWilderness() || !game::GetWorldMap())
    {
      std::cout << "  FAIL the resumed game is not on the world map"
		<< std::endl;
      ++Failures;
    }

    if(!game::GetPlayer() || !game::GetPlayer()->GetSquareUnder()
       || game::GetPlayer()->GetPos() != v2(X, Y))
    {
      std::cout << "  FAIL the resumed world-map player is not on its tile"
		<< std::endl;
      ++Failures;
    }

    if(!game::GetPlayer() || game::GetPlayer()->GetMoney() != Money)
    {
      std::cout << "  FAIL the resumed world-map player lost a mutation"
		<< std::endl;
      ++Failures;
    }

    return Failures ? 1 : 0;
  }

  if(!strcmp(Phase, "saveenv"))
  {
    BootstrapGame();
    game::SetPlayerNameForTest(Name);

    /* Refuse the very first entry to Attnam, then save the game while the town
       is still unentered, so the pending weather has to survive the whole
       save/load round trip and not only an immediate retry. The refusal is
       forced for the same reason as the in-process check: the town's water is
       seed-dependent, so an ordinary companion is not a reliable way to make
       the placement search fail. */
    dungeon* Town = game::GetDungeon(ATTNAM);

    if(Town->GetLevel(0))
      Town->UnloadLevel(0);

    remove(Town->GetLevelFileName(game::SaveName(), 0).CStr());
    Town->SetIsGenerated(0, false);

    level* Source = SetupLocalSource(WILDERNESS_JUNGLE, TestSlot(1800));
    v2 Orig(Source->GetXSize() / 2, Source->GetYSize() / 2);
    character* P = game::GetPlayer();

    if(P->GetSquareUnder())
      P->Remove();

    PlaceCharacter(P, Source, Orig);

    game::ForcePlacementFailureForTest();
    truth Refused;
    {
      relationguard Guard;
      Refused = game::TryTravel(ATTNAM, 0, 0, false, false);
    }

    if(Refused)
    {
      std::cout << "  FAIL the first attnam entry was not refused" << std::endl;
      return 1;
    }

    game::Save(Base);
    return 0;
  }

  if(!strcmp(Phase, "loadenv"))
  {
    int Failures = 0;

    if(!game::Init(Name))
    {
      std::cout << "  FAIL Continue Game could not resume " << Name
		<< std::endl;
      return 1;
    }

    {
      relationguard Guard;

      if(!game::TryTravel(ATTNAM, 0, 0, false, false))
      {
	std::cout << "  FAIL could not enter attnam after a refused first entry"
		  << std::endl;
	++Failures;
      }
    }

    if(!Failures)
    {
      liquid* Rain = game::GetCurrentLevel()->GetGlobalRainLiquid();

      if(!Rain || game::GetGlobalRainLiquid() != Rain)
      {
	std::cout << "  FAIL attnam lost its weather across a save/load"
		  << std::endl;
	++Failures;
      }

      if(Rain)
	Rain->GetVolume();
    }

    return Failures ? 1 : 0;
  }

  if(!strcmp(Phase, "arena"))
  {
    /* G2: the real sumo mirror lifecycle at production unload boundaries. The
       town is written with the default deleting save, so the original player
       and the town's own sumo are released the moment the arena is entered,
       exactly as TryToEnterSumoArena() does. The player and the sumo are both
       mirrored and introduced to the arena, and on the way back the mirrors
       are destroyed with Player set to null so EnterArea() has to recover the
       original from the reloaded town. Runs in its own process because the
       production path deletes live characters and reloads a level. */
    BootstrapGame();

    /* The room-master lookup is cached against the game tick and is only
       meaningful once the clock has moved, as it always has by the time the
       real arena is used. */
    game::IncreaseTick();
    game::IncreaseTick();

    int Failures = 0;
    dungeon* D = game::GetDungeon(NEW_ATTNAM);

    for(int l = 0; l < D->GetLevels(); ++l)
    {
      if(D->GetLevel(l))
	D->UnloadLevel(l);

      remove(LevelFilePath(D, l).CStr());
      D->SetIsGenerated(l, false);
    }

    level* Town = SetupLocalSource(NEW_ATTNAM, 0);
    character* Original = game::GetPlayer();

    /* A deterministic return square: pin the exit entry and stand the original
       on it, exactly where the arena trigger leaves them. */
    v2 Entry = Town->GetEntryPos(0, STAIRS_DOWN);

    if(Entry == ERROR_V2)
      Entry = v2(Town->GetXSize() / 2, Town->GetYSize() / 2);

    Town->SetEntryPos(STAIRS_DOWN, Entry);
    PlaceCharacter(Original, Town, Entry);
    Original->SetMoney(4242);
    ulong OriginalID = Original->GetID();

    /* The town's own sumo, the very character the production handoff mirrors. */
    character* OrigSumo = game::GetSumo();

    if(!OrigSumo)
    {
      std::cout << "  FAIL the town had no sumo to mirror" << std::endl;
      return 1;
    }

    /* The mirror handoff, for both the player and the wrestler. */
    character* Mirror = Original->Duplicate(IGNORE_PROHIBITIONS);
    character* MirrorSumo = OrigSumo->Duplicate(IGNORE_PROHIBITIONS);
    game::SetPlayer(Mirror);

    /* The production save deletes the town, so the original player and sumo do
       not stay live while the mirrors occupy the arena. */
    D->SaveLevel(game::SaveName(), 0);

    if(D->GetLevel(0))
    {
      std::cout << "  FAIL the town stayed loaded after the sumo save"
		<< std::endl;
      ++Failures;
    }

    charactervector Empty;

    if(!game::EnterArea(Empty, 1, STAIRS_UP))
    {
      std::cout << "  FAIL the mirror could not enter the arena" << std::endl;
      return 1;
    }

    level* Arena = game::GetCurrentLevel();

    /* Introduce the mirror wrestler the way production does. */
    MirrorSumo->PutTo(SUMO_ARENA_POS + v2(6, 5));
    MirrorSumo->ChangeTeam(game::GetTeam(SUMO_TEAM));
    Arena->GetLSquare(SUMO_ARENA_POS)->GetRoom()->SetMasterID(MirrorSumo->GetID());

    if(InactivePlacedCount())
    {
      std::cout << "  FAIL the arena entry left " << InactivePlacedCount()
		<< " enabled creature(s) off the active area" << std::endl;
      ++Failures;
    }

    /* An ordinary tick has to be safe with the mirror and the wrestler up. */
    pool::Be();
    pool::BurnHell();

    if(InactivePlacedCount())
    {
      std::cout << "  FAIL a pool tick during the bout revived an off-screen "
		   "area" << std::endl;
      ++Failures;
    }

    /* An item and a companion left in the arena, to be carried back. */
    v2 ArenaPos = Arena->GetEntryPos(0, STAIRS_UP);

    if(ArenaPos == ERROR_V2)
      ArenaPos = v2(Arena->GetXSize() / 2, Arena->GetYSize() / 2);

    item* ArenaItem = banana::Spawn();
    ulong ArenaItemID = ArenaItem->GetID();
    Arena->GetLSquare(ArenaPos)->AddItem(ArenaItem);
    character* Companion = AddFollower("wolf", Arena, ArenaPos);

    if(!Companion)
    {
      std::cout << "  FAIL could not place an arena companion" << std::endl;
      ++Failures;
    }

    /* Dispose of the mirror wrestler, then the mirror player, exactly as
       EndSumoWrestling does, and ask EnterArea for the original with no player
       set. */
    character* RoomSumo = Arena->GetLSquare(SUMO_ARENA_POS)->GetRoom()->GetMaster();

    if(RoomSumo)
    {
      RoomSumo->Remove();
      delete RoomSumo;
    }

    Mirror->Remove();
    delete Mirror;
    game::SetPlayer(0);

    itemvector IVector;
    charactervector CVector;
    Arena->CollectEverything(IVector, CVector);
    D->SaveLevel(game::SaveName(), 1);

    if(D->GetLevel(1))
    {
      std::cout << "  FAIL the arena stayed loaded after the return save"
		<< std::endl;
      ++Failures;
    }

    charactervector Empty2;

    if(!game::EnterArea(Empty2, 0, STAIRS_DOWN))
    {
      std::cout << "  FAIL the sumo return found no usable place" << std::endl;
      return 1;
    }

    character* Recovered = game::GetPlayer();

    if(!Recovered || !Recovered->GetSquareUnder())
    {
      std::cout << "  FAIL the original player was not recovered" << std::endl;
      ++Failures;
    }
    else if(Recovered->GetID() != OriginalID || Recovered->GetMoney() != 4242)
    {
      std::cout << "  FAIL the recovered player is not the original"
		<< std::endl;
      ++Failures;
    }

    if(DuplicateCharacterIDs())
    {
      std::cout << "  FAIL the sumo return left duplicate character ids"
		<< std::endl;
      ++Failures;
    }

    if(InactivePlacedCount())
    {
      std::cout << "  FAIL the sumo return left " << InactivePlacedCount()
		<< " enabled creature(s) off the active area" << std::endl;
      ++Failures;
    }

    /* Carry the arena's items and characters back, as production does. */
    if(Recovered)
    {
      Recovered->GetStackUnder()->AddItems(IVector);
      v2 PlayerPos = Recovered->GetPos();

      for(uint c = 0; c < CVector.size(); ++c)
	CVector[c]->PutNear(PlayerPos);

      if(!LevelHasItem(game::GetCurrentLevel(), ArenaItemID))
      {
	std::cout << "  FAIL the arena item was not carried back" << std::endl;
	++Failures;
      }

      if(Companion && !Companion->GetSquareUnder())
      {
	std::cout << "  FAIL the arena companion was not restored" << std::endl;
	++Failures;
      }
    }

    return Failures ? 1 : 0;
  }

  return 1;
}

int WildernessTest(int Seeds)
{
  /* S3.6: reject a seed count the assertions were not written for instead of
     quietly running a degenerate batch. */
  if(Seeds < 1 || Seeds > 4096)
  {
    std::cout << "FAIL seed count " << Seeds << " outside 1..4096" << std::endl;
    std::cout << "WILDERNESS TEST FAILED: 1 failures" << std::endl;
    return 1;
  }

  /* T5: Main() established the isolated environment before any configuration
     or graphics startup; reaching here without it means that step was skipped,
     which is a failure rather than a reason to keep going. */
  if(!WildernessTestTmp[0])
  {
    std::cout << "FAIL the isolated run directory was not established"
	      << std::endl;
    std::cout << "WILDERNESS TEST FAILED: 1 failures" << std::endl;
    return 1;
  }

  int TotalFailures = 0;

  BootstrapGame();

  /* R6/S5: encoded-slot boundaries, container capacity, deliberately
     mismatched profiles being rejected rather than merely agreeing, and
     original destinations keeping their own identity. */
  {
    int Failures = 0;

    if(WildernessSlotFromTileID(254) != 254) ++Failures;
    if(WildernessSlotFromTileID(255) != 256) ++Failures;
    if(WildernessSlotFromTileID(256) != 257) ++Failures;
    if(WildernessSlotFromTileID(16383) != 16384) ++Failures;
    if(WildernessTileIDFromSlot(254) != 254) ++Failures;
    if(WildernessTileIDFromSlot(256) != 255) ++Failures;
    if(WildernessTileIDFromSlot(257) != 256) ++Failures;
    if(WildernessTileIDFromSlot(16384) != 16383) ++Failures;

    for(int t = 0; t < WILDERNESS_TILE_COUNT; ++t)
      if(WildernessSlotFromTileID(t) == WORLD_MAP)
	++Failures;

    for(int s = 0; s < Seeds; ++s)
    {
      int Slot = TestSlot(s);

      if(Slot < 0 || Slot >= WILDERNESS_LEVEL_SLOTS || Slot == WORLD_MAP)
	++Failures;
    }

    const levelscript* Foreign =
      game::GetDungeon(ELPURI_CAVE)->GetLevelScript(0);

    for(int d = WILDERNESS_DUNGEON_FIRST; d <= WILDERNESS_DUNGEON_LAST; ++d)
    {
      dungeon* D = game::GetDungeon(d);

      if(D->GetLevels() != WILDERNESS_LEVEL_SLOTS)
	++Failures;

      festring Why;
      const levelscript* Default = D->GetLevelScript(0);

      /* The shipped profile must fit ... */
      if(!dungeon::WildernessProfileFits(Default, WILDERNESS_LEVEL_SLOTS, Why))
      {
	std::cout << "  FAIL " << Why.CStr() << std::endl;
	++Failures;
      }

      /* ... a short container must not ... */
      Why = "";

      if(dungeon::WildernessProfileFits(Default, WILDERNESS_LEVEL_SLOTS - 1, Why))
	++Failures;
      else if(!Why.GetSize())
	++Failures;

      /* ... and neither may an original, non-wilderness profile. */
      Why = "";

      if(dungeon::WildernessProfileFits(Foreign, WILDERNESS_LEVEL_SLOTS, Why))
	++Failures;
      else if(!Why.GetSize())
	++Failures;
    }

    /* S3.8: no original destination may be claimed by a wilderness container,
       and every other scripted destination must still resolve. */
    const std::map<int, dungeonscript>& Script =
      game::GetGameScript()->GetDungeon();

    for(std::map<int, dungeonscript>::const_iterator i = Script.begin();
	i != Script.end(); ++i)
    {
      int d = i->first;
      dungeon* D = game::GetDungeon(d);

      if(!D)
      {
	++Failures;
	continue;
      }

      if(game::IsWildernessDungeon(d))
      {
	if(d < WILDERNESS_DUNGEON_FIRST || d > WILDERNESS_DUNGEON_LAST
	   || D->GetLevels() != WILDERNESS_LEVEL_SLOTS)
	  ++Failures;
      }
      else if(D->GetLevels() <= 0 || !D->GetLevelScript(0))
	++Failures;
    }

    if(game::IsWildernessDungeon(ELPURI_CAVE)
       || game::IsWildernessDungeon(ATTNAM)
       || game::IsWildernessDungeon(NEW_ATTNAM)
       || game::GetDungeon(ELPURI_CAVE)->GetLevels() < 2)
      ++Failures;

    std::cout << (Failures ? "FAIL " : "ok   ")
	      << "slots and profiles capacity=" << WILDERNESS_LEVEL_SLOTS
	      << " failures=" << Failures << std::endl;
    TotalFailures += Failures;
  }

  /* S3.1/S3.2/S3.4: entry, corridor clearance, population whitelist resolved by
     configuration id, and biome-appropriate vegetation/obstacle terrain. */
  bool BatchWide = Seeds >= 8;

  for(int b = 0; b < TestBiomeCount; ++b)
  {
    const testbiome& Biome = TestBiomes[b];
    int AllowedSize;
    const testallow* Allowed = GetAllowedTable(Biome.Type, AllowedSize);
    biomecount Count(64, 48);
    int EntryFailures = 0;
    int ReachFailures = 0;
    int CorridorFail = 0;

    for(int s = 0; s < Seeds; ++s)
    {
      femath::SetSeed(1000003 * (b + 1) + s);

      dungeon D(Biome.Dungeon);
      int Slot = TestSlot(b * 4096 + s);
      game::SetIsGenerating(true);
      D.PrepareLevel(Slot, false);
      game::SetIsGenerating(false);
      level* L = game::GetCurrentLevel();
      v2 Center(L->GetXSize() / 2, L->GetYSize() / 2);

      v2 Entry = L->GetEntryPos(0, WILDERNESS_LOCAL_ENTRY);

      /* F4: the arrival is a naturalized dell nudged up to two squares off the
	 exact centre, so it only has to stay near it. */
      if((Entry - Center).GetLengthSquare() > 8)
	++EntryFailures;

      if(Biome.Type != OCEAN_LEVEL
	 && !(L->GetLSquare(Entry)->GetWalkability() & WALK))
	++EntryFailures;

      if(Biome.Type != OCEAN_LEVEL && !ReachesMapEdge(L, Entry))
	++ReachFailures;

      if(Biome.Type != OCEAN_LEVEL)
	CorridorFail += CorridorFailures(L);

      TallyBiome(L, Biome.Type, Allowed, AllowedSize, Biome.Name, Count, s);

      game::SetCurrentArea(0);
      game::SetCurrentLevel(0);
    }

    int TerrainFailures = 0;

    if(GuaranteedVegetation(Biome.Type) && Count.EmptyVegetationSeeds)
    {
      std::cout << "  FAIL " << Biome.Name << " produced "
		<< Count.EmptyVegetationSeeds << " seed(s) with no vegetation"
		<< std::endl;
      ++TerrainFailures;
    }

    if(GuaranteedObstacles(Biome.Type) && Count.EmptyObstacleSeeds)
    {
      std::cout << "  FAIL " << Biome.Name << " produced "
		<< Count.EmptyObstacleSeeds << " seed(s) with no obstacles"
		<< std::endl;
      ++TerrainFailures;
    }

    if(BatchWide && HasVegetation(Biome.Type) && Count.Vegetation == 0)
    {
      std::cout << "  FAIL " << Biome.Name
		<< " produced no vegetation across the whole batch" << std::endl;
      ++TerrainFailures;
    }

    if(BatchWide && Biome.Type != OCEAN_LEVEL && Count.Obstacles == 0)
    {
      std::cout << "  FAIL " << Biome.Name
		<< " produced no obstacles across the whole batch" << std::endl;
      ++TerrainFailures;
    }

    /* One rock in one corner of a whole batch is not a landscape: a biome that
       is meant to carry plants has to reach a good part of the map. Steppe and
       tundra are legitimately near-treeless, so they are exempt. */
    if(BatchWide && GuaranteedVegetation(Biome.Type)
       && Count.Spread(Count.VegCells) < 4)
    {
      std::cout << "  FAIL " << Biome.Name << " vegetation only reached "
		<< Count.Spread(Count.VegCells) << " map cell(s)" << std::endl;
      ++TerrainFailures;
    }

    /* T4: per seed, not a single-map threshold over the whole batch total. */
    if(Biome.Type == OCEAN_LEVEL && Count.ShallowWaterSeeds)
    {
      std::cout << "  FAIL ocean produced " << Count.ShallowWaterSeeds
		<< " seed(s) that were mostly land" << std::endl;
      ++TerrainFailures;
    }

    /* E6: the intended structure as a broad band, checked on the batch's own
       minimum and maximum so one unlucky seed cannot hide a generator that is
       systematically too sparse or too dense. */
    if(BatchWide)
    {
      double BandMin, BandMax;
      BiomeCoverageBand(Biome.Type, BandMin, BandMax);

      if(Count.MinCoverage * 100 < BandMin
	 || Count.MaxCoverage * 100 > BandMax)
      {
	std::cout << "  FAIL " << Biome.Name << " vegetation coverage "
		  << Count.MinCoverage * 100 << ".." << Count.MaxCoverage * 100
		  << "% is outside the intended band " << BandMin << ".."
		  << BandMax << "%" << std::endl;
	++TerrainFailures;
      }
    }

    if(Count.BadTreeConfig)
      ++TerrainFailures;

    /* E6: structure, not just total plant count. If the generators regress
       towards a few isolated groves, the fraction of cells near vegetation
       collapses back towards the old counts. */
    if(BatchWide && (Biome.Type == JUNGLE || Biome.Type == LEAFY_FOREST
		     || Biome.Type == EVERGREEN_FOREST)
       && Count.MinNear * 100 < 20)
    {
      std::cout << "  FAIL " << Biome.Name
		<< " vegetation is too fragmented: only " << Count.MinNear * 100
		<< "% of the map is in or next to it" << std::endl;
      ++TerrainFailures;
    }

    /* F1/F4: a jungle that looks dense but stays fully transparent offers no
       concealment at all, which an earlier review measured at 0% opaque. */
    if(BatchWide && Biome.Type == JUNGLE && Count.MinOpaque * 100 < 3)
    {
      std::cout << "  FAIL jungle has only " << Count.MinOpaque * 100
		<< "% opaque foliage; its canopy gives no line-of-sight cover"
		<< std::endl;
      ++TerrainFailures;
    }

    /* F1: the understory must resist movement, not only look dense. */
    if(BatchWide && Biome.Type == JUNGLE)
    {
      if(!Count.Thickets)
      {
	std::cout << "  FAIL jungle produced no impassable thicket"
		  << std::endl;
	++TerrainFailures;
      }

      if(Count.WalkableThickets)
      {
	std::cout << "  FAIL " << Count.WalkableThickets
		  << " jungle thicket cell(s) are still walkable" << std::endl;
	++TerrainFailures;
      }

      /* Brush stops feet, not wings or ghosts, and stays choppable. */
      if(Count.FlyBlockedThickets || Count.EtherealBlockedThickets)
      {
	std::cout << "  FAIL jungle thicket blocks "
		  << Count.FlyBlockedThickets << " flying / "
		  << Count.EtherealBlockedThickets << " ethereal cell(s)"
		  << std::endl;
	++TerrainFailures;
      }

      if(Count.UndestroyableThickets)
      {
	std::cout << "  FAIL " << Count.UndestroyableThickets
		  << " jungle thicket cell(s) cannot be destroyed"
		  << std::endl;
	++TerrainFailures;
      }
    }

    /* F4/F2: a glacier is a continuous ice surface, not snow-covered ground
       studded with wall islands, so a real fraction of its floor must be
       exposed ice. */
    if(BatchWide && Biome.Type == GLACIER && Count.MinIceGround * 100 < 8)
    {
      std::cout << "  FAIL glacier has only " << Count.MinIceGround * 100
		<< "% exposed ice ground; it is reading as snow with walls"
		<< std::endl;
      ++TerrainFailures;
    }

    int Failures = EntryFailures + ReachFailures + CorridorFail
      + Count.BadCreatures + TerrainFailures;

    std::cout << (Failures ? "FAIL " : "ok   ") << Biome.Name
	      << " creatures=" << Count.Creatures
	      << " veg=" << Count.Vegetation
	      << " obst=" << Count.Obstacles
	      << " water=" << Count.Water
	      << " vegSpread=" << Count.Spread(Count.VegCells)
	      << " cover=" << Count.MinCoverage * 100 << ".."
	      << Count.MaxCoverage * 100
	      << " near=" << Count.MinNear * 100 << ".." << Count.MaxNear * 100
	      << " opaque=" << Count.MinOpaque * 100 << ".."
	      << Count.MaxOpaque * 100
	      << " open=" << Count.MinOpen * 100 << ".." << Count.MaxOpen * 100
	      << " ice=" << Count.MinIceGround * 100 << ".."
	      << Count.MaxIceGround * 100
	      << " entryFail=" << EntryFailures
	      << " reachFail=" << ReachFailures
	      << " corridorFail=" << CorridorFail
	      << " badTree=" << Count.BadTreeConfig
	      << " bad=" << Count.BadCreatures << std::endl;

    TotalFailures += Failures;
  }

  /* S3.5: the oasis is optional per map, so it gets its own bounded search
     instead of being demanded from every seed. */
  {
    int Failures = 0;
    truth Found = false;
    const int MaxSeeds = 64;

    for(int s = 0; s < MaxSeeds && !Found; ++s)
    {
      femath::SetSeed(31337 + s);
      dungeon D(WILDERNESS_DESERT);
      game::SetIsGenerating(true);
      D.PrepareLevel(TestSlot(8000 + s), false);
      game::SetIsGenerating(false);
      Found = CountWater(game::GetCurrentLevel()) != 0;
      game::SetCurrentArea(0);
      game::SetCurrentLevel(0);
    }

    if(!Found)
    {
      std::cout << "  FAIL no oasis in " << MaxSeeds << " desert seeds"
		<< std::endl;
      ++Failures;
    }

    std::cout << (Failures ? "FAIL " : "ok   ") << "desert oasis failures="
	      << Failures << std::endl;
    TotalFailures += Failures;
  }

  /* S3.3/S4: a save must round-trip the serialized fields exactly as written,
     must not resurrect or relocate anybody, and must re-derive the fields
     level::Save() deliberately omits. The script values used here are non-zero,
     so dropping the restoration would be caught. */
  {
    int Failures = 0;

    struct poisoncase
    {
      int Dungeon;
      int Slot;
      const char* Name;
    };

    poisoncase Cases[] =
    {
      { WILDERNESS_JUNGLE, 0, "jungle" },
      { ELPURI_CAVE,       0, "elpuricave" }
    };

    for(uint c = 0; c < sizeof(Cases) / sizeof(poisoncase); ++c)
    {
      femath::SetSeed(987654 + c);

      game::SetCurrentDungeonIndex(Cases[c].Dungeon);
      game::SetCurrentLevelIndex(Cases[c].Slot);
      game::SetIsInWilderness(false);

      dungeon* Original = new dungeon(Cases[c].Dungeon);
      festring SaveName = "wildernesstest";
      festring FileName = Original->GetLevelFileName(SaveName, Cases[c].Slot);

      game::SetIsGenerating(true);
      Original->PrepareLevel(Cases[c].Slot, false);
      game::SetIsGenerating(false);

      level* L = game::GetCurrentLevel();
      int ScriptMinus = L->GetEnchantmentMinusChance();
      int ScriptPlus = L->GetEnchantmentPlusChance();
      std::vector<std::pair<ulong, v2> > Before;
      CollectOccupants(L, Before);

      L->SetDifficultyForTest(123.456);
      L->SetMonsterGenerationIntervalForTest(777);
      L->SetIdealPopulationForTest(4242);
      L->SetEnchantmentMinusChanceForTest(9991);
      L->SetEnchantmentPlusChanceForTest(9992);

      {
	outputfile Out(FileName);
	L->Save(Out);
      }

      game::SetCurrentArea(0);
      game::SetCurrentLevel(0);
      delete Original;

      dungeon* Reloaded = new dungeon(Cases[c].Dungeon);
      level* Loaded = Reloaded->LoadLevel(SaveName, Cases[c].Slot);
      std::vector<std::pair<ulong, v2> > After;
      CollectOccupants(Loaded, After);

      if(Loaded->GetDifficulty() != 123.456
	 || Loaded->GetMonsterGenerationInterval() != 777
	 || Loaded->GetIdealPopulation() != 4242)
      {
	std::cout << "  FAIL " << Cases[c].Name
		  << " serialized stats not preserved: diff="
		  << Loaded->GetDifficulty()
		  << " gen=" << Loaded->GetMonsterGenerationInterval()
		  << " pop=" << Loaded->GetIdealPopulation() << std::endl;
	++Failures;
      }

      if(Loaded->GetEnchantmentMinusChance() != ScriptMinus
	 || Loaded->GetEnchantmentPlusChance() != ScriptPlus)
      {
	std::cout << "  FAIL " << Cases[c].Name
		  << " nonserialized stats not restored: "
		  << Loaded->GetEnchantmentMinusChance() << "/"
		  << Loaded->GetEnchantmentPlusChance()
		  << " expected " << ScriptMinus << "/" << ScriptPlus
		  << std::endl;
	++Failures;
      }

      if(Before.size() != After.size() || Before != After)
      {
	std::cout << "  FAIL " << Cases[c].Name
		  << " occupants changed across the round trip: "
		  << Before.size() << " -> " << After.size() << std::endl;
	++Failures;
      }

      /* Revisiting must find exactly the persisted population: there is no
         respawn in a wilderness area. */
      game::SetCurrentArea(0);
      game::SetCurrentLevel(0);
      delete Reloaded;

      dungeon* Visit = new dungeon(Cases[c].Dungeon);
      level* Again = Visit->LoadLevel(SaveName, Cases[c].Slot);
      std::vector<std::pair<ulong, v2> > Third;
      CollectOccupants(Again, Third);

      if(Third != After)
      {
	std::cout << "  FAIL " << Cases[c].Name
		  << " revisit produced a different population" << std::endl;
	++Failures;
      }

      game::SetCurrentArea(0);
      game::SetCurrentLevel(0);
      delete Visit;
      remove(FileName.CStr());
    }

    std::cout << (Failures ? "FAIL " : "ok   ")
	      << "save round trip failures=" << Failures << std::endl;
    TotalFailures += Failures;
  }

  /* level::CheckSunLight() derives the whole day/night cycle from the game
     tick, and a wilderness area is outdoors, so it has to survive the
     transition rather than only the moment it was generated in. */
  {
    int Failures = 0;
    ulong SavedTick = game::GetTick();
    level* L = SetupLocalSource(WILDERNESS_JUNGLE, TestSlot(1100));

    game::SetTickForTest(0);              /* cos(0) == 1, full day */
    L->CheckSunLight();
    col24 DaySun = L->GetSunLightEmitation();
    col24 DayAmbient = L->GetAmbientLuminance();

    game::SetTickForTest(12000);          /* cos(pi/2) == 0, night */
    L->CheckSunLight();
    col24 NightSun = L->GetSunLightEmitation();
    col24 NightAmbient = L->GetAmbientLuminance();
    col24 NightFloor = L->GetNightAmbientLuminance();

    game::SetTickForTest(SavedTick);

    if(!DaySun || DaySun == NightSun)
    {
      std::cout << "  FAIL day sun " << DaySun << " vs night sun " << NightSun
		<< std::endl;
      ++Failures;
    }

    if(NightSun || NightAmbient != NightFloor || DayAmbient == NightAmbient)
    {
      std::cout << "  FAIL night sun " << NightSun << " ambient "
		<< NightAmbient << " floor " << NightFloor << std::endl;
      ++Failures;
    }

    game::SetCurrentArea(0);
    game::SetCurrentLevel(0);

    std::cout << (Failures ? "FAIL " : "ok   ")
	      << "day and night failures=" << Failures << std::endl;
    TotalFailures += Failures;
  }

  /* S1/S2 acceptance: incompatible movement modes, a companion that cannot
     follow, an obstructed arrival square and a non-wilderness transfer must
     all either finish with a valid placement or leave the source exactly as it
     was found. */
  {
    int Failures = 0;
    character* PlayerChar = game::GetPlayer();

    /* 1. Walking player into an all-water area: refuse, restore the source. */
    {
      level* Source = SetupLocalSource(WILDERNESS_JUNGLE, TestSlot(1000));
      v2 Orig(Source->GetXSize() / 2, Source->GetYSize() / 2);
      PlaceCharacter(PlayerChar, Source, Orig);

      relationguard Guard;
      truth Result = game::TryTravel(WILDERNESS_OCEAN, TestSlot(1001),
				     WILDERNESS_LOCAL_ENTRY, false, false);

      if(Result)
      {
	std::cout << "  FAIL ocean transfer accepted a walking player"
		  << std::endl;
	++Failures;
      }

      if(game::GetCurrentLevel() != Source || game::IsInWilderness())
      {
	std::cout << "  FAIL source area not restored" << std::endl;
	++Failures;
      }

      if(!PlayerChar->GetSquareUnder() || PlayerChar->GetPos() != Orig)
      {
	std::cout << "  FAIL player not restored to " << Orig.X << "," << Orig.Y
		  << " but " << SafePos(PlayerChar).CStr() << std::endl;
	++Failures;
      }
    }

    /* 2. A swim-only companion that cannot stand on the destination. */
    {
      level* Source = SetupLocalSource(WILDERNESS_JUNGLE, TestSlot(1010));
      v2 Orig(Source->GetXSize() / 2, Source->GetYSize() / 2);
      PlaceCharacter(PlayerChar, Source, Orig);
      EnsureWaterNear(Source, Orig);
      character* Dolphin = AddFollower("dolphin", Source, Orig);

      if(!Dolphin)
      {
	std::cout << "  FAIL could not spawn a dolphin follower" << std::endl;
	++Failures;
      }
      else
      {
	v2 DolphinPos = Dolphin->GetPos();
	relationguard Guard;
	truth Result = game::TryTravel(WILDERNESS_LEAFY_FOREST, TestSlot(1011),
				       WILDERNESS_LOCAL_ENTRY, false, true);

	if(Result)
	{
	  std::cout << "  FAIL transfer accepted a swim-only follower on land"
		    << std::endl;
	  ++Failures;
	}

	if(game::GetCurrentLevel() != Source)
	{
	  std::cout << "  FAIL source area not restored for the follower"
		    << std::endl;
	  ++Failures;
	}

	if(!PlayerChar->GetSquareUnder() || PlayerChar->GetPos() != Orig)
	{
	  std::cout << "  FAIL player not restored after the follower refusal"
		    << std::endl;
	  ++Failures;
	}

	if(!Dolphin->GetSquareUnder() || Dolphin->GetPos() != DolphinPos)
	{
	  std::cout << "  FAIL companion not restored to its own square"
		    << std::endl;
	  ++Failures;
	}

	Dolphin->Remove();
	delete Dolphin;
      }
    }

    /* 3. A companion that can follow: everybody lands on a usable square. */
    {
      level* Source = SetupLocalSource(WILDERNESS_JUNGLE, TestSlot(1020));
      v2 Orig(Source->GetXSize() / 2, Source->GetYSize() / 2);
      PlaceCharacter(PlayerChar, Source, Orig);
      character* Wolf = AddFollower("wolf", Source, Orig);

      if(!Wolf)
      {
	std::cout << "  FAIL could not spawn a wolf follower" << std::endl;
	++Failures;
      }
      else
      {
	relationguard Guard;
	truth Result = game::TryTravel(WILDERNESS_LEAFY_FOREST, TestSlot(1021),
				       WILDERNESS_LOCAL_ENTRY, false, true);

	if(!Result)
	{
	  std::cout << "  FAIL transfer refused a walkable follower" << std::endl;
	  ++Failures;
	}
	else
	{
	  Failures += PlacementFailures(PlayerChar, "player on leafyforest");

	  if(Wolf->GetSquareUnder() && PlayerChar->GetSquareUnder()
	     && Wolf->GetPos() == PlayerChar->GetPos())
	  {
	    std::cout << "  FAIL player and companion share a square"
		      << std::endl;
	    ++Failures;
	  }

	  Failures += PlacementFailures(Wolf, "wolf on leafyforest");
	}

	Wolf->Remove();
	delete Wolf;
      }
    }

    /* 4. A fully obstructed arrival square: either the transfer finds another
          place to stand or it refuses without touching the source. */
    {
      const int DestSlot = TestSlot(1031);
      dungeon* DestDungeon = game::GetDungeon(WILDERNESS_EVERGREEN_FOREST);
      game::SetIsInWilderness(false);
      game::SetCurrentDungeonIndex(WILDERNESS_EVERGREEN_FOREST);
      game::SetCurrentLevelIndex(DestSlot);
      game::SetIsGenerating(true);
      DestDungeon->PrepareLevel(DestSlot, false);
      game::SetIsGenerating(false);

      level* Dest = DestDungeon->GetLevel(DestSlot);
      v2 Center(Dest->GetXSize() / 2, Dest->GetYSize() / 2);

      for(int dx = -2; dx <= 2; ++dx)
	for(int dy = -2; dy <= 2; ++dy)
	  Dest->GetLSquare(Center + v2(dx, dy))
	    ->ChangeOLTerrain(wall::Spawn(ICE_WALL));

      /* EnterArea() reloads a generated level from disk, so the blocked map has
         to be on disk before the transfer is attempted. */
      DestDungeon->SaveLevel(game::SaveName(), DestSlot, false);

      level* Source = SetupLocalSource(WILDERNESS_JUNGLE, TestSlot(1030));
      v2 Orig(Source->GetXSize() / 2, Source->GetYSize() / 2);
      PlaceCharacter(PlayerChar, Source, Orig);

      relationguard Guard;
      truth Result = game::TryTravel(WILDERNESS_EVERGREEN_FOREST, DestSlot,
				     WILDERNESS_LOCAL_ENTRY, false, false);

      if(Result)
      {
	Failures += PlacementFailures(PlayerChar, "player past an obstruction");

	if(PlayerChar->GetPos() == Center)
	{
	  std::cout << "  FAIL player was placed on a wall" << std::endl;
	  ++Failures;
	}
      }
      else
      {
	if(game::GetCurrentLevel() != Source)
	{
	  std::cout << "  FAIL source area not restored after obstruction"
		    << std::endl;
	  ++Failures;
	}

	if(!PlayerChar->GetSquareUnder() || PlayerChar->GetPos() != Orig)
	{
	  std::cout << "  FAIL player not restored after obstruction"
		    << std::endl;
	  ++Failures;
	}
      }
    }

    /* 5. An original dungeon transfer uses the very same code path. */
    {
      level* Source = SetupLocalSource(ELPURI_CAVE, 0);
      v2 Entry = Source->GetEntryPos(PlayerChar, STAIRS_DOWN);

      if(Entry == ERROR_V2 || !Source->IsValidPos(Entry))
	Entry = v2(Source->GetXSize() / 2, Source->GetYSize() / 2);

      PlaceCharacter(PlayerChar, Source, Entry);

      relationguard Guard;
      truth Result = game::TryTravel(ELPURI_CAVE, 1, STAIRS_UP, false, false);

      if(!Result)
      {
	std::cout << "  FAIL original dungeon transfer refused" << std::endl;
	++Failures;
      }
      else
	Failures += PlacementFailures(PlayerChar, "player in elpuricave level 1");
    }

    /* 6. And the same for an original town, since EnterArea() is shared and
          its consequences are not limited to the wilderness. */
    if(game::GetDungeon(NEW_ATTNAM)->GetLevels() >= 2)
    {
      level* Source = SetupLocalSource(NEW_ATTNAM, 0);
      v2 Entry = Source->GetEntryPos(PlayerChar, STAIRS_DOWN);

      if(Entry == ERROR_V2 || !Source->IsValidPos(Entry))
	Entry = v2(Source->GetXSize() / 2, Source->GetYSize() / 2);

      PlaceCharacter(PlayerChar, Source, Entry);

      relationguard Guard;
      truth Result = game::TryTravel(NEW_ATTNAM, 1, STAIRS_UP, false, false);

      if(!Result)
      {
	std::cout << "  FAIL original town transfer refused" << std::endl;
	++Failures;
      }
      else
	Failures += PlacementFailures(PlayerChar, "player in newattnam level 1");
    }

    /* 7. A hostile creature in sight blocks the departure, and the source has
          to be intact afterwards. */
    {
      level* Source = SetupLocalSource(WILDERNESS_JUNGLE, TestSlot(1040));
      v2 Orig(Source->GetXSize() / 2, Source->GetYSize() / 2);
      PlaceCharacter(PlayerChar, Source, Orig);
      character* Foe = AddFollower("wolf", Source, Orig);

      if(!Foe)
      {
	std::cout << "  FAIL could not spawn the blocking hostile" << std::endl;
	++Failures;
      }
      else
      {
	team* PlayerSide = game::GetTeam(PLAYER_TEAM);
	team* MonsterSide = game::GetTeam(MONSTER_TEAM);
	int PlayerToMonster = PlayerSide->GetRelation(MonsterSide);
	int MonsterToPlayer = MonsterSide->GetRelation(PlayerSide);
	PlayerSide->SetRelation(MonsterSide, HOSTILE);
	MonsterSide->SetRelation(PlayerSide, HOSTILE);
	Foe->ChangeTeam(MonsterSide);

	truth Result = game::TryTravel(WILDERNESS_LEAFY_FOREST, TestSlot(1041),
				       WILDERNESS_LOCAL_ENTRY, false, true);

	if(Result)
	{
	  std::cout << "  FAIL escaped with a hostile creature in sight"
		    << std::endl;
	  ++Failures;
	}

	if(game::GetCurrentLevel() != Source
	   || !PlayerChar->GetSquareUnder() || PlayerChar->GetPos() != Orig)
	{
	  std::cout << "  FAIL source not restored after the hostile block"
		    << std::endl;
	  ++Failures;
	}

	PlayerSide->SetRelation(MonsterSide, PlayerToMonster);
	MonsterSide->SetRelation(PlayerSide, MonsterToPlayer);

	Foe->Remove();
	delete Foe;
      }
    }

    std::cout << (Failures ? "FAIL " : "ok   ")
	      << "travel transfers failures=" << Failures << std::endl;
    TotalFailures += Failures;
  }

  /* S1's original reproduction: entering a wilderness area straight off a
     synthetic world map, for both a refused and an accepted tile. */
  {
    int Failures = 0;
    character* PlayerChar = game::GetPlayer();

    worldmap* Map = new worldmap(WORLD_MAP_WIDTH, WORLD_MAP_HEIGHT);
    /* The constructor leaves every tile ocean; give one tile a biome the
       walking player can actually enter. */
    v2 JunglePos(WORLD_MAP_WIDTH / 2, WORLD_MAP_HEIGHT / 2);
    Map->GetWSquare(JunglePos)->SetGWTerrain(jungle::Spawn());

    game::SetWorldMapForTest(Map);
    game::SetIsInWilderness(true);
    game::SetCurrentArea(Map);
    game::SetCurrentWSquareMap(Map->GetMap());
    game::SetCurrentLevel(0);
    game::SetCurrentLSquareMap(0);

    v2 OceanPos(WORLD_MAP_WIDTH / 2 + 4, WORLD_MAP_HEIGHT / 2);

    if(PlayerChar->GetSquareUnder())
      PlayerChar->Remove();

    PlayerChar->PutTo(OceanPos);

    truth Refused = game::TryEnterWilderness(OceanPos);

    if(Refused)
    {
      std::cout << "  FAIL world entry accepted a walking player on ocean"
		<< std::endl;
      ++Failures;
    }

    if(!game::IsInWilderness() || !PlayerChar->GetSquareUnder()
       || PlayerChar->GetPos() != OceanPos)
    {
      std::cout << "  FAIL world map state not restored after a refusal"
		<< std::endl;
      ++Failures;
    }

    if(PlayerChar->GetSquareUnder())
      PlayerChar->Remove();

    PlayerChar->PutTo(JunglePos);

    if(!game::TryEnterWilderness(JunglePos))
    {
      std::cout << "  FAIL world entry refused a walkable biome tile"
		<< std::endl;
      ++Failures;
    }
    else if(game::IsInWilderness())
    {
      std::cout << "  FAIL still on the world map after entering" << std::endl;
      ++Failures;
    }
    else
      Failures += PlacementFailures(PlayerChar, "player after world entry");

    /* A follower that came along has to ride back in the world group rather
       than being dropped or left standing on the deleted area. */
    character* ExitWolf = 0;

    if(!game::IsInWilderness())
    {
      ExitWolf = AddFollower("wolf", game::GetCurrentLevel(), PlayerChar->GetPos());

      if(!ExitWolf)
      {
	std::cout << "  FAIL could not spawn a follower for the exit"
		  << std::endl;
	++Failures;
      }
    }

    /* Leaving again must put the player back on the very same world tile; this
       is exactly the expression the off-map walk in char.cpp builds. */
    if(!game::IsInWilderness())
    {
      int Slot = game::GetCurrentLevelIndex();
      int TileID = WildernessTileIDFromSlot(Slot);
      int DungeonID = game::GetCurrentDungeonIndex();
      /* Same neutralisation the other travel tests use: with hostiles in sight
	 CollectCreatures() refuses, which would mask the transfer itself. */
      relationguard Guard;
      truth Returned = game::TryTravel(
	WORLD_MAP, WORLD_MAP,
	game::IsWildernessDungeon(DungeonID)
	  ? WildernessReturnEntry(TileID) : DungeonID);

      if(!Returned)
      {
	std::cout << "  FAIL leaving the wilderness area was refused" << std::endl;
	++Failures;
      }
      else if(!game::IsInWilderness() || !game::GetWorldMap())
      {
	std::cout << "  FAIL not back on the world map" << std::endl;
	++Failures;
      }
      else
      {
	v2 WorldPos(TileID % WORLD_MAP_WIDTH, TileID / WORLD_MAP_WIDTH);

	if(!PlayerChar->GetSquareUnder() || PlayerChar->GetPos() != WorldPos)
	{
	  std::cout << "  FAIL left at " << SafePos(PlayerChar).CStr()
		    << " instead of the entered tile "
		    << WorldPos.X << "," << WorldPos.Y << std::endl;
	  ++Failures;
	}

	if(ExitWolf)
	{
	  charactervector& WorldGroup = game::GetWorldMap()->GetPlayerGroup();
	  truth Found = false;

	  for(uint c = 0; c < WorldGroup.size(); ++c)
	    if(WorldGroup[c] == ExitWolf)
	      Found = true;

	  if(!Found)
	  {
	    std::cout << "  FAIL the follower was not carried back to the "
			 "world map" << std::endl;
	    ++Failures;
	  }

	  /* Take it out again so the check above leaves no stray team member for
	     the later pool tick. */
	  for(charactervector::iterator i = WorldGroup.begin();
	      i != WorldGroup.end(); ++i)
	    if(*i == ExitWolf)
	    {
	      WorldGroup.erase(i);
	      break;
	    }

	  delete ExitWolf;
	}
      }
    }

    std::cout << (Failures ? "FAIL " : "ok   ")
	      << "world entry failures=" << Failures << std::endl;
    TotalFailures += Failures;
  }

  /* T1: a refused destination must not stay alive in the entity pool. A map
     nobody stands on may not keep its creatures enabled, ticking and counting
     for team checks while the player continues somewhere else. */
  {
    int Failures = 0;
    character* PlayerChar = game::GetPlayer();
    const int SourceSlot = TestSlot(1200);
    const int DestSlot = TestSlot(1201);
    dungeon* DestDungeon = game::GetDungeon(WILDERNESS_LEAFY_FOREST);
    level* Source = SetupLocalSource(WILDERNESS_JUNGLE, SourceSlot);
    v2 Orig(Source->GetXSize() / 2, Source->GetYSize() / 2);
    PlaceCharacter(PlayerChar, Source, Orig);
    EnsureWaterNear(Source, Orig);
    character* Dolphin = AddFollower("dolphin", Source, Orig);

    if(!Dolphin)
    {
      std::cout << "  FAIL could not spawn the dolphin for the release test"
		<< std::endl;
      ++Failures;
    }
    else
    {
      /* Every level an earlier check left loaded is a source of off-screen
	 characters; release them so the count below measures only this
	 transfer. */
      UnloadInactiveLevels(WILDERNESS_JUNGLE, SourceSlot);
      int Baseline = PlacedEnabledCount();
      truth Result;

      {
	relationguard Guard;
	Result = game::TryTravel(WILDERNESS_LEAFY_FOREST, DestSlot,
				 WILDERNESS_LOCAL_ENTRY, false, true);
      }

      if(Result)
      {
	std::cout << "  FAIL a transfer that cannot place everyone was accepted"
		  << std::endl;
	++Failures;
      }

      if(game::GetCurrentLevel() != Source)
      {
	std::cout << "  FAIL source not restored after the release test"
		  << std::endl;
	++Failures;
      }

      if(DestDungeon->GetLevel(DestSlot))
      {
	std::cout << "  FAIL the refused destination is still loaded"
		  << std::endl;
	++Failures;
      }

      int After = PlacedEnabledCount();

      if(After != Baseline)
      {
	std::cout << "  FAIL the refused destination left " << (After - Baseline)
		  << " enabled creature(s) behind" << std::endl;
	++Failures;
      }

      /* The refused map has to have been written out, or the next visit would
	 look for a file that was never created. */
      festring DestFile = LevelFilePath(DestDungeon, DestSlot);
      struct stat St;

      if(stat(DestFile.CStr(), &St) || St.st_size <= 0)
      {
	std::cout << "  FAIL the refused destination was not written to "
		  << DestFile.CStr() << std::endl;
	++Failures;
      }

      /* With only the active area live, an ordinary pool tick has to be safe
	 and must not resurrect anything off-screen. */
      pool::Be();
      pool::BurnHell();

      if(PlacedEnabledCount() > Baseline)
      {
	std::cout << "  FAIL ticking the pool grew the live population"
		  << std::endl;
	++Failures;
      }

      Dolphin->Remove();
      delete Dolphin;
      Dolphin = 0;

      /* Re-entering has to load the file the refusal wrote. */
      PlaceCharacter(PlayerChar, Source, Orig);
      truth Reloaded;

      {
	relationguard Again;
	Reloaded = game::TryTravel(WILDERNESS_LEAFY_FOREST, DestSlot,
				   WILDERNESS_LOCAL_ENTRY, false, false);
      }

      if(!Reloaded)
      {
	std::cout << "  FAIL re-entry into the refused destination failed"
		  << std::endl;
	++Failures;
      }
      else if(game::GetCurrentDungeonIndex() != WILDERNESS_LEAFY_FOREST
	      || game::GetCurrentLevelIndex() != DestSlot)
      {
	std::cout << "  FAIL re-entry landed in the wrong container"
		  << std::endl;
	++Failures;
      }
      else
	Failures += PlacementFailures(PlayerChar, "player in the reloaded area");
    }

    std::cout << (Failures ? "FAIL " : "ok   ")
	      << "refused destination release failures=" << Failures
	      << std::endl;
    TotalFailures += Failures;
  }

  /* T2: the global rain binding belongs to the active area. It has to be
     installed on arrival, survive the destruction of a rainy source, be
     restored on refusal and come back with a town that is reloaded from disk. */
  {
    int Failures = 0;
    character* PlayerChar = game::GetPlayer();

    struct towncase
    {
      int Dungeon;
      const char* Name;
    };

    towncase Towns[] =
    {
      { ATTNAM, "attnam" },
      { NEW_ATTNAM, "newattnam" }
    };

    for(uint t = 0; t < sizeof(Towns) / sizeof(towncase); ++t)
    {
      dungeon* Town = game::GetDungeon(Towns[t].Dungeon);

      if(Town->GetLevels() < 1)
      {
	std::cout << "  FAIL " << Towns[t].Name << " has no level to test"
		  << std::endl;
	++Failures;
	continue;
      }

      /* Build the town from scratch through the production path, so the code
	 that installs a town's rain actually runs. */
      if(Town->GetLevel(0))
	Town->UnloadLevel(0);

      remove(LevelFilePath(Town, 0).CStr());
      Town->SetIsGenerated(0, false);

      level* Source = SetupLocalSource(WILDERNESS_JUNGLE, TestSlot(1500 + t));
      v2 Orig(Source->GetXSize() / 2, Source->GetYSize() / 2);
      PlaceCharacter(PlayerChar, Source, Orig);
      truth Entered;

      {
	relationguard Guard;
	Entered = game::TryTravel(Towns[t].Dungeon, 0, 0, false, false);
      }

      if(!Entered)
      {
	std::cout << "  FAIL could not enter " << Towns[t].Name << std::endl;
	++Failures;
	continue;
      }

      level* TownLevel = game::GetCurrentLevel();
      liquid* TownRain = TownLevel->GetGlobalRainLiquid();
      v2 TownSpeed = TownLevel->GetGlobalRainSpeed();

      if(!TownRain || game::GetGlobalRainLiquid() != TownRain
	 || game::GetGlobalRainSpeed() != TownSpeed)
      {
	std::cout << "  FAIL " << Towns[t].Name
		  << " did not install its own rain on arrival" << std::endl;
	++Failures;
      }

      /* Ordinary play dereferences the binding on every tick. */
      if(game::GetGlobalRainLiquid())
	game::GetGlobalRainLiquid()->GetVolume();

      /* Leaving deletes the rainy source; the destination is a wilderness
	 area that owns its own (possibly dormant) weather binding, so the
	 active binding must become the destination's rather than dangle at the
	 destroyed town. */
      truth Left;

      {
	relationguard Guard;
	Left = game::TryTravel(WILDERNESS_JUNGLE, TestSlot(1501 + t),
			       WILDERNESS_LOCAL_ENTRY, false, false);
      }

      if(!Left)
      {
	std::cout << "  FAIL could not leave " << Towns[t].Name << std::endl;
	++Failures;
      }

      if(game::GetGlobalRainLiquid()
	 != game::GetCurrentLevel()->GetGlobalRainLiquid())
      {
	std::cout << "  FAIL leaving " << Towns[t].Name
		  << " left a dangling rain binding" << std::endl;
	++Failures;
      }

      /* The wilderness tile travels through the production entry path, so it
	 must own its weather cycle and be the active binding now. */
      if(!game::GetCurrentLevel()->HasWeather())
      {
	std::cout << "  FAIL the wilderness entered after leaving "
		  << Towns[t].Name << " has no weather cycle" << std::endl;
	++Failures;
      }

      /* Returning loads the town from disk; its rain has to come back. */
      truth Returned;

      {
	relationguard Guard;
	Returned = game::TryTravel(Towns[t].Dungeon, 0, 0, false, false);
      }

      if(!Returned)
      {
	std::cout << "  FAIL could not re-enter " << Towns[t].Name << std::endl;
	++Failures;
	continue;
      }

      liquid* ReloadedRain = game::GetCurrentLevel()->GetGlobalRainLiquid();

      if(!ReloadedRain || game::GetGlobalRainLiquid() != ReloadedRain)
      {
	std::cout << "  FAIL " << Towns[t].Name
		  << " lost its rain when reloaded" << std::endl;
	++Failures;
      }

      if(game::GetGlobalRainLiquid())
	game::GetGlobalRainLiquid()->GetVolume();

      /* Refusal 1: a newly generated destination the walking player cannot
	 stand on. */
      {
	const int OceanSlot = TestSlot(1530 + t);
	dungeon* Ocean = game::GetDungeon(WILDERNESS_OCEAN);

	if(Ocean->GetLevel(OceanSlot))
	  Ocean->UnloadLevel(OceanSlot);

	remove(LevelFilePath(Ocean, OceanSlot).CStr());
	Ocean->SetIsGenerated(OceanSlot, false);
	truth Refused;

	{
	  relationguard Guard;
	  Refused = game::TryTravel(WILDERNESS_OCEAN, OceanSlot,
				    WILDERNESS_LOCAL_ENTRY, false, false);
	}

	if(Refused)
	{
	  std::cout << "  FAIL " << Towns[t].Name
		    << " accepted a walking player on open water" << std::endl;
	  ++Failures;
	}

	if(game::GetGlobalRainLiquid() != ReloadedRain
	   || game::GetCurrentLevel()->GetGlobalRainLiquid() != ReloadedRain
	   || game::GetGlobalRainSpeed()
	      != game::GetCurrentLevel()->GetGlobalRainSpeed())
	{
	  std::cout << "  FAIL a refused new destination stripped "
		    << Towns[t].Name << " of its rain" << std::endl;
	  ++Failures;
	}
      }

      /* Refusal 2: a destination that already exists on disk, so the refusal
	 has to undo a load rather than a generation. */
      {
	const int LoadedSlot = TestSlot(1540 + t);
	dungeon* Ocean = game::GetDungeon(WILDERNESS_OCEAN);
	level* TownNow = game::GetCurrentLevel();

	if(Ocean->GetLevel(LoadedSlot))
	  Ocean->UnloadLevel(LoadedSlot);

	remove(LevelFilePath(Ocean, LoadedSlot).CStr());
	Ocean->SetIsGenerated(LoadedSlot, false);

	/* Build and write it once, so the travel below loads it. */
	game::SetCurrentDungeonIndex(WILDERNESS_OCEAN);
	game::SetCurrentLevelIndex(LoadedSlot);
	game::SetIsGenerating(true);
	Ocean->PrepareLevel(LoadedSlot, false);
	game::SetIsGenerating(false);
	Ocean->SaveLevel(game::SaveName(), LoadedSlot, false);

	/* Back to the town, which was never left. */
	game::SetCurrentDungeonIndex(Towns[t].Dungeon);
	game::SetCurrentLevelIndex(0);
	game::SetCurrentArea(TownNow);
	game::SetCurrentLevel(TownNow);
	game::SetCurrentLSquareMap(TownNow->GetMap());
	game::SetIsInWilderness(false);
	truth Refused;

	{
	  relationguard Guard;
	  Refused = game::TryTravel(WILDERNESS_OCEAN, LoadedSlot,
				    WILDERNESS_LOCAL_ENTRY, false, false);
	}

	if(Refused)
	{
	  std::cout << "  FAIL " << Towns[t].Name
		    << " accepted a walking player on a loaded water map"
		    << std::endl;
	  ++Failures;
	}

	if(game::GetGlobalRainLiquid() != ReloadedRain
	   || game::GetCurrentLevel()->GetGlobalRainLiquid() != ReloadedRain
	   || game::GetGlobalRainSpeed()
	      != game::GetCurrentLevel()->GetGlobalRainSpeed())
	{
	  std::cout << "  FAIL a refused loaded destination stripped "
		    << Towns[t].Name << " of its rain" << std::endl;
	  ++Failures;
	}

	if(!PlayerChar->GetSquareUnder()
	   || game::GetCurrentDungeonIndex() != Towns[t].Dungeon)
	{
	  std::cout << "  FAIL " << Towns[t].Name
		    << " was not restored after refusing a loaded map"
		    << std::endl;
	  ++Failures;
	}
      }
    }

    /* Both towns now have a rainy level on disk. Transfer straight from one
       rainy source into the other loaded rainy destination: the source is
       destroyed while the destination's own binding is the active one. */
    {
      truth In;

      {
	relationguard Guard;
	In = game::TryTravel(ATTNAM, 0, 0, false, false);
      }

      if(!In)
      {
	std::cout << "  FAIL could not re-enter attnam for the rainy transfer"
		  << std::endl;
	++Failures;
      }
      else
      {
	{
	  relationguard Guard;

	  if(!game::TryTravel(NEW_ATTNAM, 0, 0, false, false))
	  {
	    std::cout << "  FAIL rainy source to rainy destination was refused"
		      << std::endl;
	    ++Failures;
	  }
	}

	liquid* Dest = game::GetCurrentLevel()->GetGlobalRainLiquid();

	if(!Dest || game::GetGlobalRainLiquid() != Dest
	   || game::GetGlobalRainSpeed() != game::GetCurrentLevel()->GetGlobalRainSpeed())
	{
	  std::cout << "  FAIL destroying the rainy source stripped the rainy "
		       "destination" << std::endl;
	  ++Failures;
	}

	if(game::GetGlobalRainLiquid())
	  game::GetGlobalRainLiquid()->GetVolume();
      }
    }

    std::cout << (Failures ? "FAIL " : "ok   ")
	      << "rain lifecycle failures=" << Failures << std::endl;
    TotalFailures += Failures;
  }

  /* G4: every enterable wilderness biome owns a cycle of clear and
     precipitation spells. The type must match the climate (rain or snow), each
     state must show or hide the drops, the wind must never be a zero vector,
     and every drawn spell length must sit inside the biome's own profile. */
  {
    int Failures = 0;

    struct testweather
    {
      int Dungeon;
      const char* Name;
      truth Snow;
    };

    const testweather Biomes[] =
    {
      { WILDERNESS_JUNGLE,           "jungle",        false },
      { WILDERNESS_LEAFY_FOREST,     "leafyforest",   false },
      { WILDERNESS_EVERGREEN_FOREST, "evergreenforest", true },
      { WILDERNESS_STEPPE,           "steppe",        false },
      { WILDERNESS_DESERT,           "desert",        false },
      { WILDERNESS_TUNDRA,           "tundra",        true },
      { WILDERNESS_GLACIER,          "glacier",       true },
      { WILDERNESS_OCEAN,            "ocean",         false }
    };

    for(uint b = 0; b < sizeof(Biomes) / sizeof(testweather); ++b)
    {
      femath::SetSeed(777000 + b);
      level* L = SetupLocalSource(Biomes[b].Dungeon, TestSlot(1950 + b));
      game::InitializeLevelEnvironment();
      game::SetGlobalRainLiquid(L->GetGlobalRainLiquid());
      game::SetGlobalRainSpeed(L->GetGlobalRainSpeed());

      if(!L->HasWeather())
      {
	std::cout << "  FAIL " << Biomes[b].Name << " has no weather cycle"
		  << std::endl;
	++Failures;
	continue;
      }

      liquid* Mat = L->GetGlobalRainLiquid();

      if(!Mat || Mat->IsPowder() != Biomes[b].Snow)
      {
	std::cout << "  FAIL " << Biomes[b].Name
		  << " carries the wrong precipitation type" << std::endl;
	++Failures;
      }

      int TotalRains = 0, EnabledRains = 0;
      CountRains(L, TotalRains, EnabledRains);

      if(!TotalRains)
      {
	std::cout << "  FAIL " << Biomes[b].Name
		  << " created no precipitation squares" << std::endl;
	++Failures;
      }

      truth AnyWet = false;

      for(int s = 0; s < WEATHER_STATE_COUNT; ++s)
      {
	L->ForceWeatherForTest(s, 500);
	long Volume = Mat ? Mat->GetVolume() : 0;
	int Expected = L->GetWeatherStateVolumeForTest(s);

	if(Expected > 0)
	  AnyWet = true;

	CountRains(L, TotalRains, EnabledRains);

	if(Volume != Expected || ((Volume > 0) != (EnabledRains > 0)))
	{
	  std::cout << "  FAIL " << Biomes[b].Name << " state " << s
		    << " has the wrong intensity or visibility" << std::endl;
	  ++Failures;
	}

	if(L->GetGlobalRainSpeed().GetLengthSquare() <= 0
	   || game::GetGlobalRainSpeed() != L->GetGlobalRainSpeed())
	{
	  std::cout << "  FAIL " << Biomes[b].Name
		    << " installed a zero or stale wind" << std::endl;
	  ++Failures;
	}

	/* The wind must reach the drops themselves, not just the binding. */
	if(!L->GlobalRainsHaveSpeedForTest(L->GetGlobalRainSpeed()))
	{
	  std::cout << "  FAIL " << Biomes[b].Name
		    << " left drops falling at a stale speed" << std::endl;
	  ++Failures;
	}
      }

      if(!AnyWet)
      {
	std::cout << "  FAIL " << Biomes[b].Name
		  << " can never precipitate" << std::endl;
	++Failures;
      }

      /* Transitions from every state must land on a valid state whose length is
	 inside that biome's profile. */
      for(int from = 0; from < WEATHER_STATE_COUNT && Failures < 200; ++from)
	for(int trial = 0; trial < 12; ++trial)
	{
	  L->ForceWeatherForTest(from, 0);
	  L->UpdateWeather();
	  int State = L->GetWeatherState();
	  int Min = 0, Max = 0;
	  L->GetWeatherDurationBoundsForTest(State, Min, Max);

	  if(State < 0 || State >= WEATHER_STATE_COUNT
	     || L->GetWeatherTimer() < Min || L->GetWeatherTimer() > Max)
	  {
	    std::cout << "  FAIL " << Biomes[b].Name
		      << " drew a spell outside its duration bounds"
		      << std::endl;
	    ++Failures;
	    break;
	  }
	}
    }

    std::cout << (Failures ? "FAIL " : "ok   ")
	      << "weather biome cycle failures=" << Failures << std::endl;
    TotalFailures += Failures;
  }

  /* G4: a long active wet spell stays visual. No standing liquid may
     accumulate, and repeated ticks must not grow the per-square rain lists. */
  {
    int Failures = 0;
    femath::SetSeed(888000);
    level* L = SetupLocalSource(WILDERNESS_JUNGLE, TestSlot(1960));
    game::InitializeLevelEnvironment();
    game::SetGlobalRainLiquid(L->GetGlobalRainLiquid());
    ClearCreatures(L);
    PlaceCharacter(game::GetPlayer(), L,
		   L->GetEntryPos(0, WILDERNESS_LOCAL_ENTRY));
    L->ForceWeatherForTest(WEATHER_HEAVY, 1000000);

    int FluidsBefore = CountGroundFluids(L);
    int TotalBefore = 0, EnabledBefore = 0;
    CountRains(L, TotalBefore, EnabledBefore);

    /* A long active spell: many drop ticks, as if the player stood here for a
       full storm. */
    L->TickWeatherForTest(800);

    int FluidsAfter = CountGroundFluids(L);
    int TotalAfter = 0, EnabledAfter = 0;
    CountRains(L, TotalAfter, EnabledAfter);

    if(L->GetWeatherState() != WEATHER_HEAVY)
    {
      std::cout << "  FAIL a pinned spell changed on its own" << std::endl;
      ++Failures;
    }

    if(FluidsAfter != FluidsBefore)
    {
      std::cout << "  FAIL a long wet spell accumulated " << FluidsAfter
		<< " ground fluid square(s)" << std::endl;
      ++Failures;
    }

    if(TotalAfter != TotalBefore || EnabledAfter != EnabledBefore)
    {
      std::cout << "  FAIL a wet spell changed the rain lists ("
		<< TotalBefore << " -> " << TotalAfter << ")" << std::endl;
      ++Failures;
    }

    /* Weather must not disturb the persistent map: a changed square and a
       dropped item survive every transition. */
    v2 Mark(3, 3);
    L->GetLSquare(Mark)->ChangeOLTerrain(decoration::Spawn(PALM));
    item* Kept = banana::Spawn();
    L->GetLSquare(Mark)->AddItem(Kept);
    ulong KeptID = Kept->GetID();

    for(int s = 0; s < WEATHER_STATE_COUNT; ++s)
    {
      L->ForceWeatherForTest(s, 300);
      L->TickWeatherForTest(200);
    }

    if(!L->GetLSquare(Mark)->GetOLTerrain()
       || L->GetLSquare(Mark)->GetOLTerrain()->GetConfig() != PALM
       || !LevelHasItem(L, KeptID))
    {
      std::cout << "  FAIL a weather spell disturbed the persistent map"
		<< std::endl;
      ++Failures;
    }

    std::cout << (Failures ? "FAIL " : "ok   ")
	      << "weather accumulation failures=" << Failures << std::endl;
    TotalFailures += Failures;
  }

  /* G4: weather ownership across a real transfer. A wet wilderness must keep
     its binding and phase when a destination is refused, and hand the binding
     to a town when the move succeeds. */
  {
    int Failures = 0;
    character* PlayerChar = game::GetPlayer();
    const int JungleSlot = TestSlot(1980);

    level* Jungle = SetupLocalSource(WILDERNESS_JUNGLE, JungleSlot);
    game::InitializeLevelEnvironment();
    game::SetGlobalRainLiquid(Jungle->GetGlobalRainLiquid());
    game::SetGlobalRainSpeed(Jungle->GetGlobalRainSpeed());
    PlaceCharacter(PlayerChar, Jungle,
		   Jungle->GetEntryPos(0, WILDERNESS_LOCAL_ENTRY));
    Jungle->ForceWeatherForTest(WEATHER_LIGHT, 500);
    liquid* JungleRain = Jungle->GetGlobalRainLiquid();
    int JungleState = Jungle->GetWeatherState();

    /* A refused ocean destination must leave the spell and the binding alone. */
    {
      dungeon* Ocean = game::GetDungeon(WILDERNESS_OCEAN);
      const int OceanSlot = TestSlot(1981);

      if(Ocean->GetLevel(OceanSlot))
	Ocean->UnloadLevel(OceanSlot);

      remove(LevelFilePath(Ocean, OceanSlot).CStr());
      Ocean->SetIsGenerated(OceanSlot, false);
      truth Refused;

      {
	relationguard Guard;
	Refused = game::TryTravel(WILDERNESS_OCEAN, OceanSlot,
				  WILDERNESS_LOCAL_ENTRY, false, false);
      }

      if(Refused)
      {
	std::cout << "  FAIL a walking player entered open water" << std::endl;
	++Failures;
      }

      if(game::GetGlobalRainLiquid() != JungleRain
	 || game::GetCurrentLevel() != Jungle
	 || Jungle->GetWeatherState() != JungleState)
      {
	std::cout << "  FAIL a refused destination disturbed the weather"
		  << std::endl;
	++Failures;
      }

      /* An inactive map must be released, weather and all. */
      if(Ocean->GetLevel(OceanSlot))
      {
	std::cout << "  FAIL a refused water map stayed loaded" << std::endl;
	++Failures;
      }
    }

    /* A successful transfer to a freshly generated town takes the binding. */
    {
      dungeon* Town = game::GetDungeon(NEW_ATTNAM);

      if(Town->GetLevel(0))
	Town->UnloadLevel(0);

      remove(LevelFilePath(Town, 0).CStr());
      Town->SetIsGenerated(0, false);
      truth In;

      {
	relationguard Guard;
	In = game::TryTravel(NEW_ATTNAM, 0, 0, false, false);
      }

      if(!In)
      {
	std::cout << "  FAIL could not reach the town from the wilderness"
		  << std::endl;
	++Failures;
      }
      else if(game::GetGlobalRainLiquid()
	      != game::GetCurrentLevel()->GetGlobalRainLiquid())
      {
	std::cout << "  FAIL the town did not take over the rain binding"
		  << std::endl;
	++Failures;
      }
    }

    std::cout << (Failures ? "FAIL " : "ok   ")
	      << "weather ownership failures=" << Failures << std::endl;
    TotalFailures += Failures;
  }

  /* G4: a refused first entry must not lose a wilderness map's weather cycle
     either. The material and phase are created at generation and written out
     with the refused map; entering for real resumes the same spell, and the
     binding belongs to the wilderness. */
  {
    int Failures = 0;
    character* PlayerChar = game::GetPlayer();

    struct wcase { int Dungeon; const char* Name; };
    const wcase Cases[] =
    {
      { WILDERNESS_JUNGLE,           "jungle" },
      { WILDERNESS_EVERGREEN_FOREST, "evergreenforest" }
    };

    for(uint n = 0; n < sizeof(Cases) / sizeof(wcase); ++n)
    {
      dungeon* Dest = game::GetDungeon(Cases[n].Dungeon);
      const int Slot = TestSlot(1990 + n);

      if(Dest->GetLevel(Slot))
	Dest->UnloadLevel(Slot);

      remove(LevelFilePath(Dest, Slot).CStr());
      Dest->SetIsGenerated(Slot, false);

      level* Source = SetupLocalSource(WILDERNESS_JUNGLE, TestSlot(1995 + n));
      PlaceCharacter(PlayerChar, Source,
		     Source->GetEntryPos(0, WILDERNESS_LOCAL_ENTRY));

      game::ForcePlacementFailureForTest();
      truth Refused;

      {
	relationguard Guard;
	Refused = game::TryTravel(Cases[n].Dungeon, Slot,
				  WILDERNESS_LOCAL_ENTRY, false, false);
      }

      if(Refused)
      {
	std::cout << "  FAIL the first entry to " << Cases[n].Name
		  << " was not refused" << std::endl;
	++Failures;
      }

      if(game::GetCurrentLevel() != Source)
      {
	std::cout << "  FAIL the source was not restored after refusing "
		  << Cases[n].Name << std::endl;
	++Failures;
      }

      struct stat St;

      if(stat(LevelFilePath(Dest, Slot).CStr(), &St) || St.st_size <= 0)
      {
	std::cout << "  FAIL the refused first entry to " << Cases[n].Name
		  << " wrote nothing" << std::endl;
	++Failures;
      }

      if(PlayerChar->GetSquareUnder())
	PlayerChar->Remove();

      PlaceCharacter(PlayerChar, Source,
		     Source->GetEntryPos(0, WILDERNESS_LOCAL_ENTRY));
      truth Entered;

      {
	relationguard Guard;
	Entered = game::TryTravel(Cases[n].Dungeon, Slot,
				  WILDERNESS_LOCAL_ENTRY, false, false);
      }

      if(!Entered)
      {
	std::cout << "  FAIL the second entry to " << Cases[n].Name
		  << " was refused" << std::endl;
	++Failures;
	continue;
      }

      level* W = game::GetCurrentLevel();

      if(!W->HasWeather() || !W->GetGlobalRainLiquid()
	 || game::GetGlobalRainLiquid() != W->GetGlobalRainLiquid())
      {
	std::cout << "  FAIL " << Cases[n].Name
		  << " lost its weather after a refused first entry"
		  << std::endl;
	++Failures;
      }

      int State = W->GetWeatherState();
      long Timer = W->GetWeatherTimer();

      truth Left, Back;

      {
	relationguard Guard;
	Left = game::TryTravel(WILDERNESS_JUNGLE, TestSlot(2000 + n),
			       WILDERNESS_LOCAL_ENTRY, false, false);
      }

      {
	relationguard Guard;
	Back = game::TryTravel(Cases[n].Dungeon, Slot,
			       WILDERNESS_LOCAL_ENTRY, false, false);
      }

      if(!Left || !Back)
      {
	std::cout << "  FAIL " << Cases[n].Name
		  << " could not be left/re-entered for weather" << std::endl;
	++Failures;
      }
      else if(game::GetCurrentLevel()->GetWeatherState() != State
	      || game::GetCurrentLevel()->GetWeatherTimer() != Timer)
      {
	std::cout << "  FAIL " << Cases[n].Name
		  << " changed its spell across a reload" << std::endl;
	++Failures;
      }
    }

    std::cout << (Failures ? "FAIL " : "ok   ")
	      << "weather first entry failures=" << Failures << std::endl;
    TotalFailures += Failures;
  }

  /* U1: a refused first entry must not lose the map's one-time environment.
     Preparation marks a fresh map generated and, on refusal, writes it out and
     releases it; if the weather were created only after a successful placement
     the reload would never run it and the town would stay dry forever. The
     refusal is forced rather than made terrain-dependent: a town may or may not
     hold water for a swim-only companion, so seeding the map just to make the
     search fail would be fragile. */
  {
    int Failures = 0;
    character* PlayerChar = game::GetPlayer();
    struct envcase
    {
      int Dungeon;
      int Slot;
      const char* Name;
    };

    envcase Cases[] =
    {
      { ATTNAM, 0, "attnam" },
      { NEW_ATTNAM, 0, "newattnam" },
      { ELPURI_CAVE, OREE_LAIR, "oree lair" }
    };

    for(uint n = 0; n < sizeof(Cases) / sizeof(Cases[0]); ++n)
    {
      dungeon* Dest = game::GetDungeon(Cases[n].Dungeon);

      /* Build the destination from scratch, with nothing on disk. */
      if(Dest->GetLevel(Cases[n].Slot))
	Dest->UnloadLevel(Cases[n].Slot);

      remove(LevelFilePath(Dest, Cases[n].Slot).CStr());
      Dest->SetIsGenerated(Cases[n].Slot, false);

      level* Source = SetupLocalSource(WILDERNESS_JUNGLE, TestSlot(1800 + n));
      v2 Orig(Source->GetXSize() / 2, Source->GetYSize() / 2);

      if(PlayerChar->GetSquareUnder())
	PlayerChar->Remove();

      PlaceCharacter(PlayerChar, Source, Orig);

      game::ForcePlacementFailureForTest();
      truth Refused;
      {
	relationguard Guard;
	Refused = game::TryTravel(Cases[n].Dungeon, Cases[n].Slot, 0,
				  false, false);
      }

      if(Refused)
      {
	std::cout << "  FAIL first entry to " << Cases[n].Name
		  << " was not refused" << std::endl;
	++Failures;
      }

      if(game::GetCurrentLevel() != Source)
      {
	std::cout << "  FAIL source not restored after refusing "
		  << Cases[n].Name << std::endl;
	++Failures;
      }

      /* The refused map must have been written with its weather included. */
      struct stat St;

      if(stat(LevelFilePath(Dest, Cases[n].Slot).CStr(), &St) || St.st_size <= 0)
      {
	std::cout << "  FAIL the refused first entry to " << Cases[n].Name
		  << " wrote nothing" << std::endl;
	++Failures;
      }

      /* Enter for real; the weather has to be waiting. */
      if(PlayerChar->GetSquareUnder())
	PlayerChar->Remove();

      PlaceCharacter(PlayerChar, Source, Orig);
      truth Entered;
      {
	relationguard Guard;
	Entered = game::TryTravel(Cases[n].Dungeon, Cases[n].Slot, 0,
				  false, false);
      }

      if(!Entered)
      {
	std::cout << "  FAIL the second entry to " << Cases[n].Name
		  << " was refused" << std::endl;
	++Failures;
	continue;
      }

      liquid* Rain = game::GetCurrentLevel()->GetGlobalRainLiquid();

      if(!Rain || game::GetGlobalRainLiquid() != Rain)
      {
	std::cout << "  FAIL " << Cases[n].Name
		  << " lost its environment after a refused first entry"
		  << std::endl;
	++Failures;
      }

      if(Rain)
	Rain->GetVolume();

      /* The player-facing half of the first entry (the automatic reveal) is
	 keyed off a serialized flag that has to survive the refusal; entering
	 for real must have completed it. */
      if(!game::GetCurrentLevel()->IsFirstEntryInitDone())
      {
	std::cout << "  FAIL " << Cases[n].Name
		  << " never completed its first-entry processing" << std::endl;
	++Failures;
      }

      /* Leaving and re-entering reloads it from disk, which is the path the
	 refusal wrote it for. */
      truth Left;
      {
	relationguard Guard;
	Left = game::TryTravel(WILDERNESS_JUNGLE, TestSlot(1810 + n),
			       WILDERNESS_LOCAL_ENTRY, false, false);
      }

      truth Back;
      {
	relationguard Guard;
	Back = game::TryTravel(Cases[n].Dungeon, Cases[n].Slot, 0, false, false);
      }

      if(!Left || !Back)
      {
	std::cout << "  FAIL " << Cases[n].Name
		  << " could not be left/re-entered" << std::endl;
	++Failures;
      }
      else
      {
	liquid* Again = game::GetCurrentLevel()->GetGlobalRainLiquid();

	if(!Again || game::GetGlobalRainLiquid() != Again)
	{
	  std::cout << "  FAIL " << Cases[n].Name
		    << " lost its environment when reloaded" << std::endl;
	  ++Failures;
	}

	if(Again)
	  Again->GetVolume();
      }
    }

    if(PlayerChar->GetSquareUnder())
      PlayerChar->Remove();

    std::cout << (Failures ? "FAIL " : "ok   ")
	      << "first entry environment failures=" << Failures << std::endl;
    TotalFailures += Failures;
  }

  /* T4: a multi-square follower needs a footprint, not just one square. In a
     one-square corridor a normal follower fits but a large creature cannot
     stand anywhere, so the transfer has to be refused rather than squeezed in. */
  {
    int Failures = 0;
    character* PlayerChar = game::GetPlayer();
    const int CorridorSlot = TestSlot(1600);
    dungeon* DestDungeon = game::GetDungeon(WILDERNESS_EVERGREEN_FOREST);

    game::SetIsInWilderness(false);
    game::SetCurrentDungeonIndex(WILDERNESS_EVERGREEN_FOREST);
    game::SetCurrentLevelIndex(CorridorSlot);
    game::SetIsGenerating(true);
    DestDungeon->PrepareLevel(CorridorSlot, false);
    game::SetIsGenerating(false);

    level* Corridor = DestDungeon->GetLevel(CorridorSlot);
    ClearCreatures(Corridor);

    /* Use the level's own arrival row so the entry square (which is nudged off
       the exact centre) stays on the one walkable row. */
    int Row = Corridor->GetEntryPos(0, WILDERNESS_LOCAL_ENTRY).Y;

    if(Row < 0 || Row >= Corridor->GetYSize())
      Row = Corridor->GetYSize() / 2;

    /* A single walkable row in an otherwise impassable sea: a normal creature
       fits there, a four-square one cannot. Water rather than a wall, because
       walls are deliberately ethereal to creatures that can break them, so a
       wall would not actually exclude anybody. */
    for(int x = 0; x < Corridor->GetXSize(); ++x)
      for(int y = 0; y < Corridor->GetYSize(); ++y)
      {
	lsquare* Sq = Corridor->GetLSquare(x, y);
	Sq->ChangeOLTerrain(0);

	if(y != Row)
	  Sq->SetLTerrain(liquidterrain::Spawn(POOL), 0);
      }

    DestDungeon->SaveLevel(game::SaveName(), CorridorSlot, false);

    /* A single-square follower fits. */
    {
      level* Source = SetupLocalSource(WILDERNESS_JUNGLE, TestSlot(1601));
      v2 Orig(Source->GetXSize() / 2, Source->GetYSize() / 2);
      PlaceCharacter(PlayerChar, Source, Orig);
      character* Wolf = AddFollower("wolf", Source, Orig);

      if(!Wolf)
      {
	std::cout << "  FAIL could not spawn the corridor follower"
		  << std::endl;
	++Failures;
      }
      else
      {
	relationguard Guard;
	truth Result = game::TryTravel(WILDERNESS_EVERGREEN_FOREST,
				       CorridorSlot,
				       WILDERNESS_LOCAL_ENTRY, false, true);

	if(!Result)
	{
	  std::cout << "  FAIL a single-square follower did not fit in a "
		       "one-square corridor" << std::endl;
	  ++Failures;
	}
	else
	{
	  Failures += PlacementFailures(PlayerChar, "player in the corridor");
	  Failures += PlacementFailures(Wolf, "follower in the corridor");
	}

	Wolf->Remove();
	delete Wolf;
      }
    }

    /* A four-square follower cannot. */
    {
      level* Source = SetupLocalSource(WILDERNESS_JUNGLE, TestSlot(1602));
      v2 Orig(Source->GetXSize() / 2, Source->GetYSize() / 2);
      PlaceCharacter(PlayerChar, Source, Orig);
      character* Huge = AddFollower("vladimir", Source, Orig);

      if(!Huge)
      {
	std::cout << "  FAIL could not spawn the large follower" << std::endl;
	++Failures;
      }
      else
      {
	if(Huge->GetSquaresUnder() != 4)
	{
	  std::cout << "  FAIL the large follower is not multi-square ("
		    << Huge->GetSquaresUnder() << ")" << std::endl;
	  ++Failures;
	}

	relationguard Guard;
	truth Result = game::TryTravel(WILDERNESS_EVERGREEN_FOREST,
				       CorridorSlot,
				       WILDERNESS_LOCAL_ENTRY, false, true);

	if(Result)
	{
	  std::cout << "  FAIL a four-square follower was squeezed into a "
		       "one-square corridor" << std::endl;
	  ++Failures;
	}

	if(game::GetCurrentLevel() != Source)
	{
	  std::cout << "  FAIL source not restored for the large follower"
		    << std::endl;
	  ++Failures;
	}

	Huge->Remove();
	delete Huge;
      }
    }

    std::cout << (Failures ? "FAIL " : "ok   ")
	      << "multi-square placement failures=" << Failures << std::endl;
    TotalFailures += Failures;
  }

  /* T4/U2: the saved game and its subordinate files have to be resumable, not
     merely present, and the resume has to happen in a process that is not
     already holding a live game (production Load() builds a new game graph
     instead of tearing an old one down). Each phase therefore runs in its own
     process through the same game::Init() the "Continue Game" menu calls, and
     the resumed state is compared against facts captured when the save was
     written: both an ordinary save and the autosave prefix, for a local area
     and for the world map. */
  {
    int Failures = 0;
    const char* Local[] = { "wildreload", "AutoSave" };

    for(uint n = 0; n < sizeof(Local) / sizeof(Local[0]); ++n)
    {
      Failures += RunResumePhase("save", Local[n]);
      Failures += RunResumePhase("load", Local[n]);
    }

    Failures += RunResumePhase("saveworld", "wildreloadsea");
    Failures += RunResumePhase("loadworld", "wildreloadsea");
    Failures += RunResumePhase("saveenv", "wildenv");
    Failures += RunResumePhase("loadenv", "wildenv");
    Failures += RunResumePhase("arena", "sumo");

    std::cout << (Failures ? "FAIL " : "ok   ")
	      << "full game resume failures=" << Failures << std::endl;
    TotalFailures += Failures;
  }

  /* The autosave must actually reach the isolated directory. */
  {
    int Failures = 0;
    festring SaveFile;
    SaveFile << WildernessTestTmp << "/IvanSave/AutoSave.sav";
    struct stat St;

    if(stat(SaveFile.CStr(), &St) || St.st_size <= 0)
    {
      std::cout << "  FAIL no autosave written to " << SaveFile.CStr()
		<< std::endl;
      ++Failures;
    }

    std::cout << (Failures ? "FAIL " : "ok   ")
	      << "autosave failures=" << Failures << std::endl;
    TotalFailures += Failures;
  }

  /* U3: the `>` command itself, not only the API it calls. On the world map
     commandsystem::GoDown() must enter a walkable biome tile and refuse an
     ocean tile, leaving the world state intact. */
  {
    int Failures = 0;
    character* PlayerChar = game::GetPlayer();

    worldmap* Map = new worldmap(WORLD_MAP_WIDTH, WORLD_MAP_HEIGHT);
    /* Fresh tiles: an earlier test may have generated levels for the tiles it
       used, and a generated slot whose file is gone may not be entered. */
    v2 JunglePos(10, 10);
    Map->GetWSquare(JunglePos)->SetGWTerrain(jungle::Spawn());
    game::SetWorldMapForTest(Map);
    game::SetIsInWilderness(true);
    game::SetCurrentArea(Map);
    game::SetCurrentWSquareMap(Map->GetMap());
    game::SetCurrentLevel(0);
    game::SetCurrentLSquareMap(0);
    game::SetCurrentDungeonIndex(WORLD_MAP);
    game::SetCurrentLevelIndex(WORLD_MAP);

    v2 OceanPos(14, 10);

    if(PlayerChar->GetSquareUnder())
      PlayerChar->Remove();

    PlayerChar->PutTo(OceanPos);

    truth Went = commandsystem::TestGoDown(PlayerChar);

    if(Went || !game::IsInWilderness() || !PlayerChar->GetSquareUnder()
       || PlayerChar->GetPos() != OceanPos)
    {
      std::cout << "  FAIL the go-down command entered an ocean tile"
		<< std::endl;
      ++Failures;
    }

    if(PlayerChar->GetSquareUnder())
      PlayerChar->Remove();

    PlayerChar->PutTo(JunglePos);

    truth Entered = commandsystem::TestGoDown(PlayerChar);

    if(!Entered || game::IsInWilderness())
    {
      std::cout << "  FAIL the go-down command did not enter a biome tile"
		<< std::endl;
      ++Failures;
    }
    else
      Failures += PlacementFailures(PlayerChar, "player after go-down command");

    std::cout << (Failures ? "FAIL " : "ok   ")
	      << "go-down command dispatch failures=" << Failures << std::endl;
    TotalFailures += Failures;
  }

  /* F3: the real sumo mirror setup, arena entry, disposal and null-player
     return run in their own subprocess (the "arena" resume phase), because the
     production path deletes a live player and reloads the town. */

  /* S5: the startup checks must abort, not merely print. */
  {
    int Failures = 0;
    Failures += RunAbortCase("clean", "profile check control", 0);
    Failures += RunAbortCase("slot", "reserved slot abort", 4);
    Failures += RunAbortCase("capacity", "capacity mismatch abort", 4);
    Failures += RunAbortCase("biome", "biome mismatch abort", 4);
    Failures += RunAbortCase("levelprofile", "level profile override abort", 4);

    std::cout << (Failures ? "FAIL " : "ok   ")
	      << "startup aborts failures=" << Failures << std::endl;
    TotalFailures += Failures;
  }
  /* U0: RemoveSaves() walks every slot of every container. It must never probe
     the reserved world-map index through a validated accessor -- doing so
     aborts the whole game on any save cleanup -- and it must only delete the
     files of the slots this game actually generated. */
  {
    int Failures = 0;
    dungeon* Jungle = game::GetDungeon(WILDERNESS_JUNGLE);
    const int GeneratedSlot = TestSlot(3000);
    const int OtherSlot = TestSlot(3001);
    const int NeverSlot = TestSlot(3002);
    struct stat St;

    /* One generated slot with both a real and an autosave level file, another
       generated slot with only a real one, the four top-level files, and two
       files that must survive untouched: the reserved slot's name and a valid
       but ungenerated slot's name. */
    SetupLocalSource(WILDERNESS_JUNGLE, GeneratedSlot);
    Jungle->SaveLevel(game::SaveName(), GeneratedSlot, false);
    Jungle->SaveLevel(game::GetAutoSaveFileName(), GeneratedSlot, false);

    SetupLocalSource(WILDERNESS_JUNGLE, OtherSlot);
    Jungle->SaveLevel(game::SaveName(), OtherSlot, false);

    if(Jungle->GetLevel(NeverSlot))
      Jungle->UnloadLevel(NeverSlot);

    Jungle->SetIsGenerated(NeverSlot, false);
    remove(Jungle->GetLevelFileName(game::SaveName(), NeverSlot).CStr());

    festring GeneratedReal =
      Jungle->GetLevelFileName(game::SaveName(), GeneratedSlot);
    festring GeneratedAuto =
      Jungle->GetLevelFileName(game::GetAutoSaveFileName(), GeneratedSlot);
    festring OtherReal = Jungle->GetLevelFileName(game::SaveName(), OtherSlot);
    festring Reserved = Jungle->GetLevelFileName(game::SaveName(), WORLD_MAP);
    festring Untouched = Jungle->GetLevelFileName(game::SaveName(), NeverSlot);
    festring RealSave = game::SaveName() + ".sav";
    festring RealWorld = game::SaveName() + ".wm";
    festring AutoSave = game::GetAutoSaveFileName() + ".sav";
    festring AutoWorld = game::GetAutoSaveFileName() + ".wm";
    const festring Touched[] = { GeneratedReal, GeneratedAuto, OtherReal,
				 Reserved, Untouched, RealSave, RealWorld,
				 AutoSave, AutoWorld };

    for(uint n = 0; n < sizeof(Touched) / sizeof(Touched[0]); ++n)
    {
      FILE* F = fopen(Touched[n].CStr(), "w");

      if(F)
	fclose(F);
    }

    for(uint n = 0; n < sizeof(Touched) / sizeof(Touched[0]); ++n)
      if(stat(Touched[n].CStr(), &St))
      {
	std::cout << "  FAIL could not stage " << Touched[n].CStr()
		  << std::endl;
	++Failures;
      }

    /* The "you died" path removes autosave output only. Reaching here at all
       proves the reserved slot was not probed. */
    game::RemoveSaves(false);

    if(!stat(GeneratedAuto.CStr(), &St) || !stat(AutoSave.CStr(), &St)
       || !stat(AutoWorld.CStr(), &St))
    {
      std::cout << "  FAIL RemoveSaves(false) left autosave files behind"
		<< std::endl;
      ++Failures;
    }

    if(stat(GeneratedReal.CStr(), &St) || stat(OtherReal.CStr(), &St)
       || stat(RealSave.CStr(), &St) || stat(RealWorld.CStr(), &St))
    {
      std::cout << "  FAIL RemoveSaves(false) deleted a real save" << std::endl;
      ++Failures;
    }

    if(stat(Reserved.CStr(), &St) || stat(Untouched.CStr(), &St))
    {
      std::cout << "  FAIL RemoveSaves(false) deleted an unrelated file"
		<< std::endl;
      ++Failures;
    }

    /* The full path removes the real saves of generated slots too, and still
       leaves the reserved and ungenerated names alone. */
    game::RemoveSaves(true);

    if(!stat(GeneratedReal.CStr(), &St) || !stat(OtherReal.CStr(), &St)
       || !stat(RealSave.CStr(), &St) || !stat(RealWorld.CStr(), &St))
    {
      std::cout << "  FAIL RemoveSaves(true) left a generated save behind"
		<< std::endl;
      ++Failures;
    }

    if(stat(Reserved.CStr(), &St) || stat(Untouched.CStr(), &St))
    {
      std::cout << "  FAIL RemoveSaves(true) deleted an unrelated file"
		<< std::endl;
      ++Failures;
    }

    std::cout << (Failures ? "FAIL " : "ok   ")
	      << "save cleanup failures=" << Failures << std::endl;
    TotalFailures += Failures;
  }


  /* S3.7: leave the temporary tree before deleting it and prove it is gone. */
  if(WildernessTestOldCwd[0])
    chdir(WildernessTestOldCwd);

  if(WildernessTestOldHome.GetSize())
    setenv("HOME", WildernessTestOldHome.CStr(), 1);
  else
    unsetenv("HOME");

  RemoveTree(WildernessTestTmp);

  {
    struct stat St;

    if(!stat(WildernessTestTmp, &St))
    {
      std::cout << "FAIL temporary directory " << WildernessTestTmp
		<< " not removed" << std::endl;
      ++TotalFailures;
    }
  }

  std::cout << (TotalFailures ? "WILDERNESS TEST FAILED: " : "WILDERNESS TEST PASSED")
	    << TotalFailures << " failures" << std::endl;
  return TotalFailures ? 1 : 0;
}


/* Main() lives in the executable translation unit and calls this directly, so
   the wrapper cannot sit in the anonymous namespace with the rest. */
bool BeginWildernessIsolation(const char* Self)
{
  return SetUpWildernessIsolation(Self);
}

#endif
