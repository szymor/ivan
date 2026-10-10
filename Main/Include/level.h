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

#ifndef __LEVEL_H__
#define __LEVEL_H__

#include <vector>
#include <queue>
#include <set>

#include "area.h"
#include "square.h"
#include "ivandef.h"

class levelscript;
class roomscript;
class squarescript;
class glterrain;
class olterrain;
class dungeon;
class lsquare;
class room;
class item;
class liquid;
class level;
class material;
class team;
struct node;
struct emitter;
template <class type> struct fearray;

struct nodepointerstorer
{
  nodepointerstorer(node* Node) : Node(Node) { }
  bool operator<(const nodepointerstorer&) const;
  node* Node;
};

struct spawnresult
{
  const character* Pioneer;
  int Seen;
};

typedef std::priority_queue<nodepointerstorer> nodequeue;
typedef std::vector<item*> itemvector;
typedef std::vector<character*> charactervector;
typedef std::vector<emitter> emittervector;
typedef std::vector<ulong> sunemittervector;
typedef character* (*characterspawner)(int, int);

struct node
{
  node(int x, int y, lsquare* Square) : Square(Square), Pos(x, y) { }
  void CalculateNextNodes();
  lsquare* Square;
  node* Last;
  v2 Pos;
  long Distance;
  long Remaining;
  long TotalDistanceEstimate;
  long Diagonals;
  truth InNodeQueue;
  truth Processed;
  static node*** NodeMap;
  static int RequiredWalkability;
  static const character* SpecialMover;
  static v2 To;
  static uchar** WalkabilityMap;
  static int XSize, YSize;
  static nodequeue* NodeQueue;
};

struct explosion
{
  character* Terrorist;
  festring DeathMsg;
  v2 Pos;
  ulong ID;
  int Strength;
  int RadiusSquare;
  int Size;
  truth HurtNeutrals;
};

struct beamdata
{
  beamdata(character*, const festring&, int, ulong);
  beamdata(character*, const festring&, v2, col16, int, int, int, ulong);
  character* Owner;
  festring DeathMsg;
  v2 StartPos;
  col16 BeamColor;
  int BeamEffect;
  int Direction;
  int Range;
  ulong SpecialParameters;
};

inline beamdata::beamdata
(
  character* Owner,
  const festring& DeathMsg,
  int Direction,
  ulong SpecialParameters
) :
Owner(Owner),
DeathMsg(DeathMsg),
Direction(Direction),
SpecialParameters(SpecialParameters)
{ }

inline beamdata::beamdata
(
  character* Owner,
  const festring& DeathMsg,
  v2 StartPos,
  col16 BeamColor,
  int BeamEffect,
  int Direction,
  int Range,
  ulong SpecialParameters
) :
Owner(Owner),
DeathMsg(DeathMsg),
StartPos(StartPos),
BeamColor(BeamColor),
BeamEffect(BeamEffect),
Direction(Direction),
Range(Range),
SpecialParameters(SpecialParameters)
{ }

class level : public area
{
 public:
  level();
  virtual ~level();
  void Generate(int);
  v2 GetRandomSquare(const character* = 0, int = 0, const rect* = 0) const;
  void GenerateMonsters();
  lsquare* GetLSquare(v2 Pos) const { return Map[Pos.X][Pos.Y]; }
  lsquare* GetLSquare(int x, int y) const { return Map[x][y]; }
  void GenerateTunnel(int, int, int, int, truth);
  void ExpandPossibleRoute(int, int, int, int, truth);
  void ExpandStillPossibleRoute(int, int, int, int, truth);
  void Save(outputfile&) const;
  void Load(inputfile&);
  void FiatLux();
  int GetIdealPopulation() const { return IdealPopulation; }
  double GetDifficulty() const { return Difficulty; }
  int GetMonsterGenerationInterval() const { return MonsterGenerationInterval; }
  void GenerateNewMonsters(int, truth = true);
  void AttachPos(int, int);
  void AttachPos(v2 Pos) { AttachPos(Pos.X, Pos.Y); }
  void CreateItems(int);
  truth MakeRoom(const roomscript*);
  void ParticleTrail(v2, v2);
  festring GetLevelMessage() { return LevelMessage; }
  void SetLevelMessage(const festring& What) { LevelMessage = What; }
  void SetLevelScript(const levelscript* What) { LevelScript = What; }
  truth IsOnGround() const;
  const levelscript* GetLevelScript() const { return LevelScript; }
  int GetLOSModifier() const;
  room* GetRoom(int) const;
  void SetRoom(int, room*);
  void AddRoom(room*);
  void Explosion(character*, const festring&, v2, int, truth = true);
  truth CollectCreatures(charactervector&, character*, truth, std::vector<v2>* = 0);
  void ApplyLSquareScript(const squarescript*);
  virtual void Draw(truth) const;
  v2 GetEntryPos(const character*, int) const;
  void GenerateRectangularRoom(std::vector<v2>&, std::vector<v2>&, std::vector<v2>&, const roomscript*, room*, v2, v2);
  void Reveal();
  static void (level::*GetBeam(int))(beamdata&);
  void ParticleBeam(beamdata&);
  void LightningBeam(beamdata&);
  void ShieldBeam(beamdata&);
  dungeon* GetDungeon() const { return Dungeon; }
  void SetDungeon(dungeon* What) { Dungeon = What; }
  int GetIndex() const { return Index; }
  void SetIndex(int What) { Index = What; }
  truth DrawExplosion(const explosion*) const;
  int TriggerExplosions(int);
  lsquare*** GetMap() const { return Map; }
  v2 GetNearestFreeSquare(const character*, v2, truth = true) const;
  v2 FreeSquareSeeker(const character*, v2, v2, int, truth) const;
  v2 GetFreeAdjacentSquare(const character*, v2, truth) const;
  static void (level::*GetBeamEffectVisualizer(int))(const fearray<lsquare*>&, col16) const;
  void ParticleVisualizer(const fearray<lsquare*>&, col16) const;
  void LightningVisualizer(const fearray<lsquare*>&, col16) const;
  truth PreProcessForBone();
  truth PostProcessForBone();
  void FinalProcessForBone();
  void GenerateDungeon(int);
  void GenerateDesert();
  void GenerateJungle();
  void GenerateSteppe();
  void GenerateLeafyForest();
  void GenerateEvergreenForest();
  void GenerateTundra();
  void GenerateGlacier();
  void GenerateOcean();
  void GenerateWilderness();
  void InitializeRuntimeStats();
  void RestoreNonSerializedStats();
  void PopulateWilderness(v2);
  void PrepareWildernessEntry();
  int GetWildernessGroundConfig() const;
  void ForceWildernessExitRoutes(v2);
  truth WildernessRouteExists(v2, v2) const;
  truth WildernessWideRouteExists(v2, v2) const;
  truth WildernessSquareReachable(v2, v2, int) const;
  void CarveWildernessTrail(v2, v2, int);
  void MakeWildernessPassable(int, int, int, truth);
  character* SpawnWildernessAnimal(const char*, const char*, v2);
  v2 FindWildernessSpawnSquare(const character*, v2, int) const;
  v2 FindTravelDestination(const character*, v2) const;
  void CreateTunnelNetwork(int, int, int, int, v2);
  void SetWalkability(v2 Pos, int What) { WalkabilityMap[Pos.X][Pos.Y] = What; }
  node* FindRoute(v2, v2, const std::set<v2>&, int, const character* = 0);
  void AddToAttachQueue(v2);
  void CollectEverything(itemvector&, charactervector&);
  void CreateGlobalRain(liquid*, v2);
  /* True until the player-facing part of the first entry (the automatic reveal,
     chiefly) has run on a map that was freshly generated. It is serialized so
     that a refused first entry -- whose map is written out and unloaded before
     anybody stands on it -- still gets that processing when the map is finally
     entered, instead of looking like an already-visited map. Environmental
     state that belongs to the map itself (rain) is created at generation and
     needs no such flag. */
  truth IsFirstEntryInitDone() const { return FirstEntryInitDone; }
  void SetFirstEntryInitDone(truth What) { FirstEntryInitDone = What; }
  /* The binding this area owns, as opposed to whichever one is currently
     installed as game::GlobalRainLiquid while a transfer is in flight. */
  liquid* GetGlobalRainLiquid() const { return GlobalRainLiquid; }
  v2 GetGlobalRainSpeed() const { return GlobalRainSpeed; }
  void CheckSunLight();
  col24 GetSunLightEmitation() const { return SunLightEmitation; }
  void InitSquarePartEmitationTicks();
  col24 GetAmbientLuminance() const { return AmbientLuminance; }
  void ForceEmitterNoxify(const emittervector&) const;
  void ForceEmitterEmitation(const emittervector&, const sunemittervector&, ulong = 0) const;
  void UpdateLOS();
  void EnableGlobalRain();
  void DisableGlobalRain();
  void InitLastSeen();
  /* Wilderness weather. A level that is a wilderness biome owns a small cycle
     of clear and precipitation spells, instead of the fixed scripted rain the
     named locations use. The phase is created at generation -- before a
     refused entry can be written out -- and is serialized, so a plain save, an
     autosave or a leave/re-enter resumes the same spell rather than rolling a
     fresh one. The schedule is seeded from the level's own tile identity, so
     two jungles never switch weather in lockstep. */
  truth HasWeather() const { return WeatherEnabled; }
  /* Wilderness precipitation is deliberately visual only: it never spills
     standing liquid, so a long spell cannot flood a map or bury dropped items,
     and it never repaints persistent terrain. */
  truth IsWeatherVisualOnly() const { return WeatherEnabled; }
  void InitializeWeather();
  void UpdateWeather();
  void ApplyWeatherState();
  int GetWeatherState() const { return WeatherState; }
  long GetWeatherTimer() const { return WeatherTimer; }
  /* Changes the precipitation wind and pushes it into every global-rain square,
     because setting only the game binding would leave the existing rain
     objects falling with their old speed. */
  void SetGlobalRainSpeed(v2);
#ifdef WILDERNESS_TEST_HARNESS
  /* Pin a state and its remaining duration so a test can drive every
     transition and every biome without waiting out thousands of ticks. */
  void ForceWeatherForTest(int State, long Duration);
  /* The duration range and intensity a biome allows for one state, so the test
     can check them against the profile rather than a hard-coded copy. */
  void GetWeatherDurationBoundsForTest(int State, int& Min, int& Max) const;
  int GetWeatherStateVolumeForTest(int State) const;
  /* Ticks this level's precipitation drops the way an active spell would,
     without stepping the whole global entity pool. */
  void TickWeatherForTest(int Times);
  truth GlobalRainsHaveSpeedForTest(v2) const;
#endif
  lsquare** GetSquareStack() const { return SquareStack; }
  col24 GetNightAmbientLuminance() const { return NightAmbientLuminance; }
  int DetectMaterial(const material*);
  void BlurMemory();
  void CalculateLuminances();
  int AddRadiusToSquareStack(v2, long) const;
  olterrain* GetRandomFountainWithWater(olterrain*) const;
  int GetEnchantmentMinusChance() { return EnchantmentMinusChance; }
  int GetEnchantmentPlusChance() { return EnchantmentPlusChance; }
#ifdef WILDERNESS_TEST_HARNESS
  /* Deliberately corrupt the fields the diagnostic round-trips: serialized ones
     must come back exactly as written, nonserialized ones as the profile says. */
  void SetDifficultyForTest(double What) { Difficulty = What; }
  void SetMonsterGenerationIntervalForTest(int What) { MonsterGenerationInterval = What; }
  void SetIdealPopulationForTest(int What) { IdealPopulation = What; }
  void SetEnchantmentMinusChanceForTest(int What) { EnchantmentMinusChance = What; }
  void SetEnchantmentPlusChanceForTest(int What) { EnchantmentPlusChance = What; }
#endif
  void Amnesia(int);
  spawnresult SpawnMonsters(characterspawner, team*, v2, int = 0, int = 1, truth = false);
 protected:
  truth GenerateLanterns(int, int, int) const;
  truth GenerateWindows(int, int) const;
  void CreateRoomSquare(glterrain*, olterrain*, int, int, int, int) const;
  void EmitSunBeams();
  void EmitSunBeam(v2, ulong, int) const;
  void ChangeSunLight();
  void EmitSunLight(v2);
  lsquare*** Map;
  const levelscript* LevelScript;
  festring LevelMessage;
  std::vector<v2> Door;
  std::vector<room*> Room;
  int IdealPopulation;
  int MonsterGenerationInterval;
  double Difficulty;
  dungeon* Dungeon;
  int Index;
  std::vector<explosion*> ExplosionQueue;
  std::vector<truth> PlayerHurt;
  node*** NodeMap;
  uchar** WalkabilityMap;
  std::vector<v2> AttachQueue;
  liquid* GlobalRainLiquid;
  v2 GlobalRainSpeed;
  col24 SunLightEmitation;
  v2 SunLightDirection;
  col24 AmbientLuminance;
  static ulong NextExplosionID;
  lsquare** SquareStack;
  col24 NightAmbientLuminance;
  int EnchantmentMinusChance;
  int EnchantmentPlusChance;
  truth FirstEntryInitDone;
  /* Wilderness weather state. WeatherEnabled is derived from the biome at
     generation and after a load, so it is not serialized; the current state,
     the ticks left in it and the per-tile random stream are, so a resumed game
     continues the same spell. */
  truth WeatherEnabled;
  int WeatherState;
  long WeatherTimer;
  ulong WeatherRandomState;
};

outputfile& operator<<(outputfile&, const level*);
inputfile& operator>>(inputfile&, level*&);

#endif
