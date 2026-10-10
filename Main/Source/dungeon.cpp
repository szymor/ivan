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

#include "dungeon.h"
#include "level.h"
#include "script.h"
#include "error.h"
#include "game.h"
#include "save.h"
#include "femath.h"

dungeon::dungeon() { }

dungeon::dungeon(int Index) : Index(Index)
{
  Initialize();

  for(int c = 0; c < GetLevels(); ++c)
    Generated[c] = false;
}

dungeon::~dungeon()
{
  for(int c = 0; c < GetLevels(); ++c)
    delete Level[c];

  delete [] Level;
  delete [] Generated;
}

void dungeon::Initialize()
{
  std::map<int, dungeonscript>::const_iterator DungeonIterator = game::GetGameScript()->GetDungeon().find(Index);

  if(DungeonIterator != game::GetGameScript()->GetDungeon().end())
    DungeonScript = &DungeonIterator->second;
  else
  {
    ABORT("Unknown dungeon #%d requested!", int(Index));
    return;
  }

  /* Validate the profile before any array is sized from it, so a bad capacity
     or a malformed LevelDefault fails clearly instead of being indexed. */
  ValidateProfile();

  Level = new level*[GetLevels()];
  Generated = new truth[GetLevels()];

  for(int c = 0; c < GetLevels(); ++c)
    Level[c] = 0;
}

/* Profile-level checks shared by every dungeon; the extra wilderness rules
   live in ValidateWildernessCapacity(). */
void dungeon::ValidateProfile() const
{
  int Levels = GetLevels();

  if(Levels <= 0)
    ABORT("Dungeon #%d declares %d levels; a container needs at least one!", Index, Levels);

  if(game::IsWildernessDungeon(Index))
    ValidateWildernessCapacity();
}

/* The single container that owns each biome. Wilderness container ids and
   biome Type values are separate enumerations that happen to describe the same
   eight regions, so the correspondence has to be stated rather than assumed. */
int dungeon::WildernessDungeonForBiome(int Type)
{
  switch(Type)
  {
   case JUNGLE: return WILDERNESS_JUNGLE;
   case LEAFY_FOREST: return WILDERNESS_LEAFY_FOREST;
   case EVERGREEN_FOREST: return WILDERNESS_EVERGREEN_FOREST;
   case STEPPE: return WILDERNESS_STEPPE;
   case DESERT: return WILDERNESS_DESERT;
   case TUNDRA: return WILDERNESS_TUNDRA;
   case GLACIER: return WILDERNESS_GLACIER;
   case OCEAN_LEVEL: return WILDERNESS_OCEAN;
   default: return 0;
  }
}

/* The world dimensions, the scripted container capacity and the profile fields
   that the slot encoding and the biome generator rely on must all agree, or a
   coordinate-derived slot could index outside Generated[]/Level[] or the map
   could be built from a profile that is not a wilderness biome. */
truth dungeon::WildernessProfileFits(const levelscript* Default, int Levels,
				     festring& Why)
{
  if(Levels != WILDERNESS_LEVEL_SLOTS)
  {
    Why << "has capacity " << Levels << " but the " << WORLD_MAP_WIDTH << 'x'
	<< WORLD_MAP_HEIGHT << " world requires " << WILDERNESS_LEVEL_SLOTS
	<< " slots";
    return false;
  }

  if(!Default)
  {
    Why << "has no LevelDefault block";
    return false;
  }

  if(!Default->GetSize())
  {
    Why << "has no local map Size";
    return false;
  }

  if(Default->GetSize()->X < 8 || Default->GetSize()->Y < 8)
  {
    Why << "declares a " << Default->GetSize()->X << 'x'
	<< Default->GetSize()->Y << " local map; at least 8x8 is required";
    return false;
  }

  if(!Default->GetType())
  {
    Why << "has no biome Type";
    return false;
  }

  int Type = *Default->GetType();

  switch(Type)
  {
   case JUNGLE:
   case LEAFY_FOREST:
   case EVERGREEN_FOREST:
   case STEPPE:
   case DESERT:
   case TUNDRA:
   case GLACIER:
   case OCEAN_LEVEL:
    break;
   default:
    Why << "uses Type " << Type << ", which is not a wilderness biome";
    return false;
  }

  /* Generation and runtime code dereferences all of these without a null
     check, so a profile that simply omits one would abort the first time a
     map was built or reloaded rather than at startup where it can be named. */
  if(!Default->IsOnGround() || !Default->GenerateMonsters()
     || !Default->GetLOSModifier())
  {
    Why << "needs IsOnGround, GenerateMonsters and LOSModifier";
    return false;
  }

  if(!Default->GetDifficultyBase() || !Default->GetMonsterAmountBase()
     || !Default->GetMonsterGenerationIntervalBase()
     || !Default->GetItemMinPriceBase()
     || !Default->GetEnchantmentMinusChanceBase()
     || !Default->GetEnchantmentPlusChanceBase()
     || !Default->GetDifficultyDelta() || !Default->GetMonsterAmountDelta()
     || !Default->GetMonsterGenerationIntervalDelta()
     || !Default->GetItemMinPriceDelta()
     || !Default->GetEnchantmentMinusChanceDelta()
     || !Default->GetEnchantmentPlusChanceDelta())
  {
    Why << "needs every *Base and *Delta statistic so a reload re-derives "
	   "the same values";
    return false;
  }

  /* A coordinate slot is not dungeon depth: every statistic that is scaled by
     the level index would drift across the container, so it must stay flat. */
  if(*Default->GetDifficultyDelta() || *Default->GetMonsterAmountDelta()
     || *Default->GetMonsterGenerationIntervalDelta()
     || *Default->GetItemMinPriceDelta()
     || *Default->GetEnchantmentMinusChanceDelta()
     || *Default->GetEnchantmentPlusChanceDelta())
  {
    Why << "must keep all *Delta statistics at zero";
    return false;
  }

  if(!Default->GetBackGroundType())
  {
    Why << "has no BackGroundType";
    return false;
  }

  if(!Default->GetDescription() || !Default->GetShortDescription())
  {
    Why << "needs Description and ShortDescription so its regions are never "
	   "shown as numbered dungeon depths";
    return false;
  }

  return true;
}

void dungeon::ValidateWildernessCapacity() const
{
  festring Why;
  const levelscript* Default = DungeonScript->GetLevelDefault();

  if(!WildernessProfileFits(Default, GetLevels(), Why))
    ABORT("Wilderness container #%d %s!", Index, Why.CStr());

  /* WildernessProfileFits() only knows that the profile names a legal biome;
     which container is allowed to hold it is this container's own business. */
  if(WildernessDungeonForBiome(*Default->GetType()) != Index)
    ABORT("Wilderness container #%d carries the profile of biome %d, which "
	  "belongs to container #%d!", Index, *Default->GetType(),
	  WildernessDungeonForBiome(*Default->GetType()));
}

void dungeon::UnloadLevel(int Number)
{
  ValidateSlot(Number);
  delete Level[Number];
  Level[Number] = 0;
}

/* A wilderness container's default profile is validated at construction, but a
   per-level override would otherwise reach generation and loading unchecked.
   The effective profile of the level actually being prepared is what builds
   the map and is indexed by the slot encoding, so it is validated as well. */
void dungeon::ValidateWildernessLevelProfile(int Level)
{
  if(game::IsWildernessDungeon(Index))
    CheckWildernessLevelProfile(GetLevelScript(Level), Level);
}

void dungeon::CheckWildernessLevelProfile(const levelscript* Effective,
					  int Level) const
{
  festring Why;

  if(!WildernessProfileFits(Effective, GetLevels(), Why))
    ABORT("Wilderness container #%d level %d %s!", Index, Level, Why.CStr());

  int ContainerType = *DungeonScript->GetLevelDefault()->GetType();
  int LevelType = *Effective->GetType();

  if(LevelType != ContainerType)
    ABORT("Wilderness container #%d level %d overrides the profile with biome "
	  "%d, but the container is biome %d!", Index, Level, LevelType,
	  ContainerType);
}

#ifdef WILDERNESS_TEST_HARNESS
void dungeon::TestValidateWildernessCapacity(int Levels) const
{
  festring Why;

  if(!WildernessProfileFits(DungeonScript->GetLevelDefault(), Levels, Why))
    ABORT("Wilderness container #%d %s!", Index, Why.CStr());
}

void dungeon::TestValidateWildernessBiome(int Type) const
{
  if(WildernessDungeonForBiome(Type) != Index)
    ABORT("Wilderness container #%d carries the profile of biome %d, which "
	  "belongs to container #%d!", Index, Type,
	  WildernessDungeonForBiome(Type));
}

void dungeon::TestValidateWildernessLevelProfile(const levelscript* Profile) const
{
  CheckWildernessLevelProfile(Profile, 0);
}
#endif

void dungeon::ValidateSlot(int I) const
{
  if(I < 0 || I >= GetLevels())
    ABORT("Dungeon #%d has no level %d (capacity %d)!", Index, I, GetLevels());

  if(game::IsWildernessDungeon(Index) && I == WORLD_MAP)
    ABORT("Wilderness container #%d rejects the reserved slot %d!", Index, I);
}

const levelscript* dungeon::GetLevelScript(int I)
{
  ValidateSlot(I);

  std::map<int, levelscript>::const_iterator LevelIterator = DungeonScript->GetLevel().find(I);

  const levelscript* LevelScript;

  if(LevelIterator != DungeonScript->GetLevel().end())
    LevelScript = &LevelIterator->second;
  else
    LevelScript = DungeonScript->GetLevelDefault();

  return LevelScript;
}

/* Returns whether the level has been visited before */

truth dungeon::PrepareLevel(int Index, truth Visual)
{
  ValidateSlot(Index);
  ValidateWildernessLevelProfile(Index);

  if(Generated[Index])
  {
    level* NewLevel = LoadLevel(game::SaveName(), Index);
    game::SetCurrentArea(NewLevel);
    game::SetCurrentLevel(NewLevel);
    game::SetCurrentLSquareMap(NewLevel->GetMap());
    return true;
  }
  else
  {
    level* NewLevel = Level[Index] = new level;
    NewLevel->SetDungeon(this);
    NewLevel->SetIndex(Index);
    NewLevel->SetLevelScript(GetLevelScript(Index));

    if(Visual)
      game::TextScreen(CONST_S("Entering ") + GetLevelDescription(Index) + CONST_S("...\n\nThis may take some time, please wait."), WHITE, false, &game::BusyAnimation);

    NewLevel->Generate(Index);
    game::SetCurrentLSquareMap(NewLevel->GetMap());
    Generated[Index] = true;
    /* A freshly generated map has not had its one-time first-entry processing
       yet; the flag survives being written out on a refused entry. */
    NewLevel->SetFirstEntryInitDone(false);
    game::BusyAnimation();

    if(*NewLevel->GetLevelScript()->GenerateMonsters())
      NewLevel->GenerateNewMonsters(NewLevel->GetIdealPopulation(), false);

    return false;
  }
}

void dungeon::SaveLevel(const festring& SaveName, int Number, truth DeleteAfterwards)
{
  ValidateSlot(Number);

  if(!Level[Number])
    ABORT("Dungeon #%d has no level %d to save!", Index, Number);

  outputfile SaveFile(GetLevelFileName(SaveName, Number));
  SaveFile << Level[Number];

  if(DeleteAfterwards)
  {
    delete Level[Number];
    Level[Number] = 0;
  }
}

level* dungeon::LoadLevel(const festring& SaveName, int Number)
{
  ValidateSlot(Number);
  inputfile SaveFile(GetLevelFileName(SaveName, Number));
  return LoadLevel(SaveFile, Number);
}

/* Wilderness containers share one global coordinate space, so their files
   need an explicit delimiter between the multi-digit dungeon id and the level
   index. Existing locations keep their historical names. */

festring dungeon::GetLevelFileName(const festring& SaveName, int Number) const
{
  if(game::IsWildernessDungeon(Index))
    return SaveName + ".wild." + Index + '.' + Number;

  return SaveName + '.' + Index + Number;
}

void dungeon::Save(outputfile& SaveFile) const
{
  SaveFile << Index << WorldMapPos;

  for(int c = 0; c < GetLevels(); ++c)
    SaveFile << Generated[c];
}

void dungeon::Load(inputfile& SaveFile)
{
  SaveFile >> Index >> WorldMapPos;
  Initialize();

  for(int c = 0; c < GetLevels(); ++c)
    SaveFile >> Generated[c];
}

int dungeon::GetLevels() const
{
  return *DungeonScript->GetLevels();
}

festring dungeon::GetLevelDescription(int I)
{
  if(GetLevel(I)->GetLevelScript()->GetDescription())
    return *GetLevel(I)->GetLevelScript()->GetDescription();
  else
    return *DungeonScript->GetDescription() + " level " + (I + 1);
}

festring dungeon::GetShortLevelDescription(int I)
{
  if(GetLevel(I)->GetLevelScript()->GetShortDescription())
    return *GetLevel(I)->GetLevelScript()->GetShortDescription();
  else
    return *DungeonScript->GetShortDescription() + " level " + (I + 1);
}

outputfile& operator<<(outputfile& SaveFile, const dungeon* Dungeon)
{
  if(Dungeon)
  {
    SaveFile.Put(1);
    Dungeon->Save(SaveFile);
  }
  else
    SaveFile.Put(0);

  return SaveFile;
}

inputfile& operator>>(inputfile& SaveFile, dungeon*& Dungeon)
{
  if(SaveFile.Get())
  {
    Dungeon = new dungeon;
    Dungeon->Load(SaveFile);
  }

  return SaveFile;
}

level* dungeon::LoadLevel(inputfile& SaveFile, int Number)
{
  ValidateSlot(Number);

  /* A refused transfer keeps its prepared destination in memory until it is
     written out; drop it before the read replaces the pointer, or the whole
     level graph would be leaked. */
  delete Level[Number];
  Level[Number] = 0;
  SaveFile >> Level[Number];
  Level[Number]->SetDungeon(this);
  Level[Number]->SetIndex(Number);
  Level[Number]->SetLevelScript(GetLevelScript(Number));

  /* Restore the script-derived statistics that level::Save() does not write.
     Difficulty, MonsterGenerationInterval and IdealPopulation are serialized
     and must be left alone; this applies to every level, not only the
     wilderness ones, because level::Save() never writes the enchantment
     chances for any level at all. */
  Level[Number]->RestoreNonSerializedStats();

  return Level[Number];
}

int dungeon::GetLevelTeleportDestination(int From) const
{
  int To;

  if(Index == ELPURI_CAVE)
  {
    if(RAND_2)
    {
      To = From + RAND_2 + RAND_2 + RAND_2 + RAND_2 + 1;

      if(To > DARK_LEVEL)
	To = From;
    }
    else
    {
      To = From - RAND_2 - RAND_2 - RAND_2 - RAND_2 - 1;

      if(To < 0)
	To = 0;
    }

    return To;
  }

  if(Index == UNDER_WATER_TUNNEL)
    return RAND_N(3);

  return From;
}
