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

#ifndef __DUNGEON_H__
#define __DUNGEON_H__

#include "v2.h"

class level;
class outputfile;
class inputfile;
class dungeonscript;
class levelscript;
class festring;

class dungeon
{
 public:
  dungeon();
  dungeon(int);
  ~dungeon();
  truth PrepareLevel(int, truth = true);
  void SaveLevel(const festring&, int, truth = true);
  level* LoadLevel(const festring&, int);
  festring GetLevelFileName(const festring&, int) const;
  level* GetLevel(int I) const { ValidateSlot(I); return Level[I]; }
  int GetLevels() const;
  void Save(outputfile&) const;
  void Load(inputfile&);
  void SetIndex(int What) { Index = What; }
  int GetIndex() const { return Index; }
  const levelscript* GetLevelScript(int);
  v2 GetWorldMapPos() { return WorldMapPos; }
  void SetWorldMapPos(v2 What) { WorldMapPos = What; }
  festring GetLevelDescription(int);
  festring GetShortLevelDescription(int);
  level* LoadLevel(inputfile&, int);
  /* Releases a prepared level that nobody is going to stand on. The container
     keeps it marked as generated and the file written first stays, so the next
     visit simply reloads it; what this does guarantee is that an inactive map
     stops holding creatures and items that would otherwise stay enabled in the
     entity pool while the player is somewhere else. */
  void UnloadLevel(int);
  truth IsGenerated(int I) const { ValidateSlot(I); return Generated[I]; }
  void SetIsGenerated(int I, truth What) { ValidateSlot(I); Generated[I] = What; }
  int GetLevelTeleportDestination(int) const;
  /* Fails clearly instead of indexing Level[]/Generated[] outside the
     container; the reserved world-map sentinel is additionally rejected for
     the wilderness containers, which never use it as a level slot. */
  void ValidateSlot(int) const;
  /* True when a profile can safely back a wilderness container; otherwise Why
     explains why not. ValidateWildernessCapacity() turns a rejection into an
     ABORT before any array is sized, and the diagnostic feeds it deliberately
     mismatched profiles to prove the rejection happens. */
  static truth WildernessProfileFits(const levelscript*, int Levels, festring& Why);
#ifdef WILDERNESS_TEST_HARNESS
  /* Runs the real startup check against an explicit capacity, so the diagnostic
     can prove a mismatched container is refused before any array is sized. */
  void TestValidateWildernessCapacity(int Levels) const;
  /* Same for the container-id/biome-identity check: the profile of another
     biome must be refused rather than quietly generated. */
  void TestValidateWildernessBiome(int Type) const;
  /* Same for the per-level override check. */
  void TestValidateWildernessLevelProfile(const levelscript*) const;
#endif
 private:
  void Initialize();
  void ValidateProfile() const;
  void ValidateWildernessCapacity() const;
  /* A wilderness container's default profile is checked at construction, but a
     per-level override would otherwise reach generation and loading unchecked.
     The effective profile of the level actually being prepared is validated
     here too, before PrepareLevel() builds or loads anything. */
  void ValidateWildernessLevelProfile(int);
  void CheckWildernessLevelProfile(const levelscript*, int) const;
  /* The one container that owns a given biome; 0 when the biome is not a
     wilderness one. A container holding another biome's profile would build
     the wrong map for the region it is reached from. */
  static int WildernessDungeonForBiome(int Type);
  const dungeonscript* DungeonScript;
  level** Level;
  int Index;
  truth* Generated;
  v2 WorldMapPos;
};

outputfile& operator<<(outputfile&, const dungeon*);
inputfile& operator>>(inputfile&, dungeon*&);

#endif
