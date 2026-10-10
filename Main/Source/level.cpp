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

/* Compiled through levelset.cpp */

#define FORBIDDEN 1
#define ON_POSSIBLE_ROUTE 2
#define STILL_ON_POSSIBLE_ROUTE 4
#define PREFERRED 8
#define ICE_TERRAIN 16
#define STONE_TERRAIN 32

level::level() : Room(1, static_cast<room*>(0)), GlobalRainLiquid(0), SunLightEmitation(0), AmbientLuminance(0), SquareStack(0), NightAmbientLuminance(0), EnchantmentMinusChance(0), EnchantmentPlusChance(0), FirstEntryInitDone(false), WeatherEnabled(false), WeatherState(WEATHER_CLEAR), WeatherTimer(0), WeatherRandomState(0) { }

/* Defined with the wilderness weather code further down; the level generator
   and the load path both need it before then. */
truth IsWeatherBiome(int);
void level::SetRoom(int I, room* What) { Room[I] = What; }
void level::AddToAttachQueue(v2 Pos) { AttachQueue.push_back(Pos); }

ulong level::NextExplosionID = 1;

node*** node::NodeMap;
int node::RequiredWalkability;
const character* node::SpecialMover;
v2 node::To;
uchar** node::WalkabilityMap;
int node::XSize, node::YSize;
nodequeue* node::NodeQueue;

level::~level()
{
  ulong c;

  for(c = 0; c < XSizeTimesYSize; ++c)
    delete NodeMap[0][c];

  for(c = 0; c < Room.size(); ++c)
    delete Room[c];

  delete [] NodeMap;
  delete [] WalkabilityMap;
  delete [] SquareStack;

  /* The global rain binding belongs to whichever area is active, and a level
     is routinely destroyed while another one already is: committing a source
     deletes it after the destination has been activated, and a refused
     destination is released while the source is active again. Clearing it
     unconditionally would therefore strip the active area of its rain, so only
     the level that actually owns the current binding may unbind it. */
  if(game::GetGlobalRainLiquid() && game::GetGlobalRainLiquid() == GlobalRainLiquid)
  {
    game::SetGlobalRainLiquid(0);
    game::SetGlobalRainSpeed(v2(0, 0));
  }

  delete GlobalRainLiquid;
}

void level::ExpandPossibleRoute(int OrigoX, int OrigoY, int TargetX, int TargetY, truth XMode)
{
#define CHECK(x, y) !(FlagMap[x][y] & (ON_POSSIBLE_ROUTE|FORBIDDEN))

#define CALL_EXPAND(x, y)\
  {\
    ExpandPossibleRoute(x, y, TargetX, TargetY, XMode);\
    \
    if(FlagMap[TargetX][TargetY] & ON_POSSIBLE_ROUTE)\
      return;\
  }

  FlagMap[OrigoX][OrigoY] |= ON_POSSIBLE_ROUTE;

  if(XMode)
  {
    if(TargetX < OrigoX)
      if(CHECK(OrigoX - 1, OrigoY))
	CALL_EXPAND(OrigoX - 1, OrigoY);

    if(TargetX > OrigoX)
      if(CHECK(OrigoX + 1, OrigoY))
	CALL_EXPAND(OrigoX + 1, OrigoY);

    if(TargetY < OrigoY)
      if(CHECK(OrigoX, OrigoY - 1))
	CALL_EXPAND(OrigoX, OrigoY - 1);

    if(TargetY > OrigoY)
      if(CHECK(OrigoX, OrigoY + 1))
	CALL_EXPAND(OrigoX, OrigoY + 1);

    if(TargetX <= OrigoX)
      if(OrigoX < XSize - 2 && CHECK(OrigoX + 1, OrigoY))
	CALL_EXPAND(OrigoX + 1, OrigoY);

    if(TargetX >= OrigoX)
      if(OrigoX > 1 && CHECK(OrigoX - 1, OrigoY))
	CALL_EXPAND(OrigoX - 1, OrigoY);

    if(TargetY <= OrigoY)
      if(OrigoY < YSize - 2 && CHECK(OrigoX, OrigoY + 1))
	CALL_EXPAND(OrigoX, OrigoY + 1);

    if(TargetY >= OrigoY)
      if(OrigoY > 1 && CHECK(OrigoX, OrigoY - 1))
	CALL_EXPAND(OrigoX, OrigoY - 1);
  }
  else
  {
    if(TargetY < OrigoY)
      if(CHECK(OrigoX, OrigoY - 1))
	CALL_EXPAND(OrigoX, OrigoY - 1);

    if(TargetY > OrigoY)
      if(CHECK(OrigoX, OrigoY + 1))
	CALL_EXPAND(OrigoX, OrigoY + 1);

    if(TargetX < OrigoX)
      if(CHECK(OrigoX - 1, OrigoY))
	CALL_EXPAND(OrigoX - 1, OrigoY);

    if(TargetX > OrigoX)
      if(CHECK(OrigoX + 1, OrigoY))
	CALL_EXPAND(OrigoX + 1, OrigoY);

    if(TargetY <= OrigoY)
      if(OrigoY < YSize - 2 && CHECK(OrigoX, OrigoY + 1))
	CALL_EXPAND(OrigoX, OrigoY + 1);

    if(TargetY >= OrigoY)
      if(OrigoY > 1 && CHECK(OrigoX, OrigoY - 1))
	CALL_EXPAND(OrigoX, OrigoY - 1);

    if(TargetX <= OrigoX)
      if(OrigoX < XSize - 2 && CHECK(OrigoX + 1, OrigoY))
	CALL_EXPAND(OrigoX + 1, OrigoY);

    if(TargetX >= OrigoX)
      if(OrigoX > 1 && CHECK(OrigoX - 1, OrigoY))
	CALL_EXPAND(OrigoX - 1, OrigoY);
  }

#undef CHECK

#undef CALL_EXPAND
}

void level::ExpandStillPossibleRoute(int OrigoX, int OrigoY, int TargetX, int TargetY, truth XMode)
{
#define CHECK(x, y) (FlagMap[x][y] & (STILL_ON_POSSIBLE_ROUTE|ON_POSSIBLE_ROUTE)) == ON_POSSIBLE_ROUTE

#define CALL_EXPAND(x, y) \
  {\
    ExpandStillPossibleRoute(x, y, TargetX, TargetY, XMode);\
    \
    if(FlagMap[TargetX][TargetY] & STILL_ON_POSSIBLE_ROUTE) \
      return;\
  }

  FlagMap[OrigoX][OrigoY] |= STILL_ON_POSSIBLE_ROUTE;

  if(XMode)
  {
    if(TargetX < OrigoX)
      if(CHECK(OrigoX - 1, OrigoY))
	CALL_EXPAND(OrigoX - 1, OrigoY);

    if(TargetX > OrigoX)
      if(CHECK(OrigoX + 1, OrigoY))
	CALL_EXPAND(OrigoX + 1, OrigoY);

    if(TargetY < OrigoY)
      if(CHECK(OrigoX, OrigoY - 1))
	CALL_EXPAND(OrigoX, OrigoY - 1);

    if(TargetY > OrigoY)
      if(CHECK(OrigoX, OrigoY + 1))
	CALL_EXPAND(OrigoX, OrigoY + 1);

    if(TargetX <= OrigoX)
      if(OrigoX < XSize - 2 && CHECK(OrigoX + 1, OrigoY))
	CALL_EXPAND(OrigoX + 1, OrigoY);

    if(TargetX >= OrigoX)
      if(OrigoX > 1 && CHECK(OrigoX - 1, OrigoY))
	CALL_EXPAND(OrigoX - 1, OrigoY);

    if(TargetY <= OrigoY)
      if(OrigoY < YSize - 2 && CHECK(OrigoX, OrigoY + 1))
	CALL_EXPAND(OrigoX, OrigoY + 1);

    if(TargetY >= OrigoY)
      if(OrigoY > 1 && CHECK(OrigoX, OrigoY - 1))
	CALL_EXPAND(OrigoX, OrigoY - 1);
  }
  else
  {
    if(TargetY < OrigoY)
      if(CHECK(OrigoX, OrigoY - 1))
	CALL_EXPAND(OrigoX, OrigoY - 1);

    if(TargetY > OrigoY)
      if(CHECK(OrigoX, OrigoY + 1))
	CALL_EXPAND(OrigoX, OrigoY + 1);

    if(TargetX < OrigoX)
      if(CHECK(OrigoX - 1, OrigoY))
	CALL_EXPAND(OrigoX - 1, OrigoY);

    if(TargetX > OrigoX)
      if(CHECK(OrigoX + 1, OrigoY))
	CALL_EXPAND(OrigoX + 1, OrigoY);

    if(TargetY <= OrigoY)
      if(OrigoY < YSize - 2 && CHECK(OrigoX, OrigoY + 1))
	CALL_EXPAND(OrigoX, OrigoY + 1);

    if(TargetY >= OrigoY)
      if(OrigoY > 1 && CHECK(OrigoX, OrigoY - 1))
	CALL_EXPAND(OrigoX, OrigoY - 1);

    if(TargetX <= OrigoX)
      if(OrigoX < XSize - 2 && CHECK(OrigoX + 1, OrigoY))
	CALL_EXPAND(OrigoX + 1, OrigoY);

    if(TargetX >= OrigoX)
      if(OrigoX > 1 && CHECK(OrigoX - 1, OrigoY))
	CALL_EXPAND(OrigoX - 1, OrigoY);
  }

#undef CHECK

#undef CALL_EXPAND
}

void level::GenerateTunnel(int FromX, int FromY, int TargetX, int TargetY, truth XMode)
{
  FlagMap[FromX][FromY] |= ON_POSSIBLE_ROUTE;
  ExpandPossibleRoute(FromX, FromY, TargetX, TargetY, XMode);
  const contentscript<glterrain>* GTerrain = LevelScript->GetTunnelSquare()->GetGTerrain();
  const contentscript<olterrain>* OTerrain = LevelScript->GetTunnelSquare()->GetOTerrain();

  if(FlagMap[TargetX][TargetY] & ON_POSSIBLE_ROUTE)
    for(int x = 0; x < XSize; ++x)
      for(int y = 0; y < YSize; ++y)
	if((FlagMap[x][y] & (ON_POSSIBLE_ROUTE|PREFERRED)) == ON_POSSIBLE_ROUTE
	   && !(x == FromX && y == FromY) && !(x == TargetX && y == TargetY))
	{
	  FlagMap[x][y] &= ~ON_POSSIBLE_ROUTE;
	  FlagMap[FromX][FromY] |= STILL_ON_POSSIBLE_ROUTE;
	  ExpandStillPossibleRoute(FromX, FromY, TargetX, TargetY, XMode);

	  if(!(FlagMap[TargetX][TargetY] & STILL_ON_POSSIBLE_ROUTE))
	  {
	    FlagMap[x][y] |= ON_POSSIBLE_ROUTE|PREFERRED;
	    Map[x][y]->ChangeGLTerrain(GTerrain->Instantiate());
	    Map[x][y]->ChangeOLTerrain(OTerrain->Instantiate());
	  }

	  for(int X = 0; X < XSize; ++X)
	    for(int Y = 0; Y < YSize; ++Y)
	      FlagMap[X][Y] &= ~STILL_ON_POSSIBLE_ROUTE;
	}

  for(int x = 1; x < XSize - 1; ++x)
    for(int y = 1; y < YSize - 1; ++y)
      FlagMap[x][y] &= ~ON_POSSIBLE_ROUTE;
}

void level::Generate(int Index)
{
  game::BusyAnimation();
  Initialize(LevelScript->GetSize()->X, LevelScript->GetSize()->Y);
  game::SetCurrentArea(this);
  game::SetCurrentLevel(this);
  Alloc2D(NodeMap, XSize, YSize);
  Alloc2D(WalkabilityMap, XSize, YSize);
  Map = reinterpret_cast<lsquare***>(area::Map);
  SquareStack = new lsquare*[XSizeTimesYSize];

  if((Index == 0 && GetDungeon()->GetIndex() == NEW_ATTNAM)
     || (Index == 0 && GetDungeon()->GetIndex() == ATTNAM))
    NightAmbientLuminance = MakeRGB24(95, 95, 95);

  int x, y;

  for(x = 0; x < XSize; ++x)
    for(y = 0; y < YSize; ++y)
    {
      Map[x][y] = new lsquare(this, v2(x, y));
      NodeMap[x][y] = new node(x, y, Map[x][y]);
    }

  for(x = 0; x < XSize; ++x)
    for(y = 0; y < YSize; ++y)
      Map[x][y]->CalculateNeighbourLSquares();

  int Type = LevelScript->GetType() ? *LevelScript->GetType() : 0;

  switch(Type)
  {
   case 0:
    GenerateDungeon(Index);
    return;
   case DESERT:
   case JUNGLE:
   case STEPPE:
   case LEAFY_FOREST:
   case EVERGREEN_FOREST:
   case TUNDRA:
   case GLACIER:
   case OCEAN_LEVEL:
    GenerateWilderness();
    return;
   default:
    ABORT("You are a terrorist. Please stop creating wterrains that are stupid.");
  }
}

void level::ApplyLSquareScript(const squarescript* Script)
{
  const interval* ScriptTimes = Script->GetTimes();
  int Times = ScriptTimes ? ScriptTimes->Randomize() : 1;

  for(int c = 0; c < Times; ++c)
  {
    v2 Pos;

    if(Script->GetPosition()->GetRandom())
      Pos = GetRandomSquare(0, Script->GetPosition()->GetFlags(), Script->GetPosition()->GetBorders());
    else
      Pos = Script->GetPosition()->GetVector();

    Map[Pos.X][Pos.Y]->ApplyScript(Script, 0);
  }
}

void level::AttachPos(int WhatX, int WhatY)
{
  int PosX = 1 + RAND() % (XSize - 2);
  int PosY = 1 + RAND() % (YSize - 2);

  while(!(FlagMap[PosX][PosY] & PREFERRED))
  {
    PosX = 1 + RAND() % (XSize - 2);
    PosY = 1 + RAND() % (YSize - 2);
  }

  FlagMap[WhatX][WhatY] &= ~FORBIDDEN;
  FlagMap[WhatX][WhatY] |= PREFERRED;
  GenerateTunnel(WhatX, WhatY, PosX, PosY, RAND() & 1);
  FlagMap[WhatX][WhatY] |= FORBIDDEN;
  FlagMap[WhatX][WhatY] &= ~PREFERRED;
}

void level::CreateItems(int Amount)
{
  if(Amount)
  {
    long MinPrice = *LevelScript->GetItemMinPriceBase() + *LevelScript->GetItemMinPriceDelta() * Index;

    for(int x = 0; x < Amount; ++x)
    {
      v2 Pos = GetRandomSquare();
      item* Item = protosystem::BalancedCreateItem(MinPrice, MAX_PRICE, ANY_CATEGORY, 0, IGNORE_BROKEN_PRICE);
      Item->CalculateEnchantment();
      Map[Pos.X][Pos.Y]->Stack->AddItem(Item);
      Item->SpecialGenerationHandler();
    }
  }
}

truth level::MakeRoom(const roomscript* RoomScript)
{
  game::BusyAnimation();
  v2 Pos = RoomScript->GetPos()->Randomize();
  v2 Size = RoomScript->GetSize()->Randomize();
  int x, y;

  if(Pos.X + Size.X > XSize - 2)
    return false;

  if(Pos.Y + Size.Y > YSize - 2)
    return false;

  for(x = Pos.X - 1; x <= Pos.X + Size.X; ++x)
    for(y = Pos.Y - 1; y <= Pos.Y + Size.Y; ++y)
      if(FlagMap[x][y] & FORBIDDEN || FlagMap[x][y] & PREFERRED)
	return false;

  room* RoomClass = protocontainer<room>::GetProto(*RoomScript->GetType())->Spawn();
  RoomClass->SetPos(Pos);
  RoomClass->SetSize(Size);
  RoomClass->SetFlags(*RoomScript->GetFlags());
  AddRoom(RoomClass);
  RoomClass->SetDivineMaster(*RoomScript->GetDivineMaster());
  game::BusyAnimation();
  std::vector<v2> OKForDoor, Inside, Border;

  GenerateRectangularRoom(OKForDoor, Inside, Border, RoomScript, RoomClass, Pos, Size);
  game::BusyAnimation();

  if(*RoomScript->GenerateFountains() && !(RAND() % 10))
    GetLSquare(Inside[RAND() % Inside.size()])->ChangeOLTerrain(fountain::Spawn());

  if(*RoomScript->AltarPossible() && !(RAND() % 5))
  {
    int Owner = 1 + RAND() % GODS;
    GetLSquare(Inside[RAND() % Inside.size()])->ChangeOLTerrain(altar::Spawn(Owner));
    game::GetGod(Owner)->SignalRandomAltarGeneration(Inside);
    RoomClass->SetDivineMaster(Owner);
  }

  if(*RoomScript->GenerateTunnel() && !Door.empty())
  {
    game::BusyAnimation();
    v2 OutsideDoorPos = Door[RAND() % Door.size()]; // An other room

    if(OKForDoor.empty())
      ABORT("The Doors - You are strange.");

    v2 InsideDoorPos = OKForDoor[RAND() % OKForDoor.size()]; // this door
    olterrain* Door = RoomScript->GetDoorSquare()->GetOTerrain()->Instantiate(); //Bug! Wrong room!

    if(Door && !(RAND() % 5) && *RoomScript->AllowLockedDoors())
    {
      if(*RoomScript->AllowBoobyTrappedDoors() && !(RAND() % 5))
	Door->CreateBoobyTrap();

      Door->Lock();
    }

    Map[OutsideDoorPos.X][OutsideDoorPos.Y]->ChangeLTerrain(RoomScript->GetDoorSquare()->GetGTerrain()->Instantiate(), Door);
    Map[OutsideDoorPos.X][OutsideDoorPos.Y]->Clean();
    FlagMap[OutsideDoorPos.X][OutsideDoorPos.Y] &= ~FORBIDDEN;
    FlagMap[OutsideDoorPos.X][OutsideDoorPos.Y] |= PREFERRED;
    FlagMap[InsideDoorPos.X][InsideDoorPos.Y] &= ~FORBIDDEN;
    FlagMap[InsideDoorPos.X][InsideDoorPos.Y] |= PREFERRED;
    Door = RoomScript->GetDoorSquare()->GetOTerrain()->Instantiate();

    if(Door && !(RAND() % 5) && *RoomScript->AllowLockedDoors())
    {
      if(*RoomScript->AllowBoobyTrappedDoors() && !(RAND() % 5))
	Door->CreateBoobyTrap();

      Door->Lock();
    }

    Map[InsideDoorPos.X][InsideDoorPos.Y]->ChangeLTerrain(RoomScript->GetDoorSquare()->GetGTerrain()->Instantiate(), Door);
    Map[InsideDoorPos.X][InsideDoorPos.Y]->Clean();
    GenerateTunnel(InsideDoorPos.X, InsideDoorPos.Y, OutsideDoorPos.X, OutsideDoorPos.Y, RAND() & 1);
    FlagMap[OutsideDoorPos.X][OutsideDoorPos.Y] |= FORBIDDEN;
    FlagMap[OutsideDoorPos.X][OutsideDoorPos.Y] &= ~PREFERRED;
    FlagMap[InsideDoorPos.X][InsideDoorPos.Y] |= FORBIDDEN;
    FlagMap[InsideDoorPos.X][InsideDoorPos.Y] &= ~PREFERRED;
  }

  if(*RoomScript->GenerateDoor())
  {
    game::BusyAnimation();
    v2 DoorPos;

    if(OKForDoor.empty())
      ABORT("The Doors - This thing has been broken.");

    DoorPos = OKForDoor[RAND() % OKForDoor.size()];
    Door.push_back(DoorPos);

    if(!*RoomScript->GenerateTunnel())
    {
      Map[DoorPos.X][DoorPos.Y]->ChangeLTerrain(RoomScript->GetDoorSquare()->GetGTerrain()->Instantiate(), RoomScript->GetDoorSquare()->GetOTerrain()->Instantiate());
      Map[DoorPos.X][DoorPos.Y]->Clean();
    }
  }

  const charactercontentmap* CharacterMap = RoomScript->GetCharacterMap();

  if(CharacterMap)
  {
    v2 CharPos(Pos + *CharacterMap->GetPos());
    const contentscript<character>* CharacterScript;

    for(int x = 0; x < CharacterMap->GetSize()->X; ++x)
    {
      game::BusyAnimation();

      for(y = 0; y < CharacterMap->GetSize()->Y; ++y)
	if(IsValidScript(CharacterScript = CharacterMap->GetContentScript(x, y)))
	{
	  character* Char = CharacterScript->Instantiate();
	  Char->SetGenerationDanger(Difficulty);

	  if(!Char->GetTeam())
	    Char->SetTeam(game::GetTeam(*LevelScript->GetTeamDefault()));

	  if(CharacterScript->GetFlags() & IS_LEADER)
	    Char->GetTeam()->SetLeader(Char);

	  Char->PutTo(CharPos + v2(x, y));
	  Char->CreateHomeData();

	  if(CharacterScript->GetFlags() & IS_MASTER)
	    RoomClass->SetMasterID(Char->GetID());
	}
    }
  }

  const itemcontentmap* ItemMap = RoomScript->GetItemMap();

  if(ItemMap)
  {
    v2 ItemPos(Pos + *ItemMap->GetPos());
    const fearray<contentscript<item> >* ItemScript;

    for(int x = 0; x < ItemMap->GetSize()->X; ++x)
    {
      game::BusyAnimation();

      for(y = 0; y < ItemMap->GetSize()->Y; ++y)
	if(IsValidScript(ItemScript = ItemMap->GetContentScript(x, y)))
	  for(uint c1 = 0; c1 < ItemScript->Size; ++c1)
	  {
	    const interval* TimesPtr = ItemScript->Data[c1].GetTimes();
	    int Times = TimesPtr ? TimesPtr->Randomize() : 1;

	    for(int c2 = 0; c2 < Times; ++c2)
	    {
	      item* Item = ItemScript->Data[c1].Instantiate();

	      if(Item)
	      {
		int SquarePosition = ItemScript->Data[c1].GetSquarePosition();

		if(SquarePosition != CENTER)
		  Item->SignalSquarePositionChange(SquarePosition);

		Map[ItemPos.X + x][ItemPos.Y + y]->GetStack()->AddItem(Item);
		Item->SpecialGenerationHandler();
	      }
	    }
	  }
    }
  }

  const glterraincontentmap* GTerrainMap = RoomScript->GetGTerrainMap();

  if(GTerrainMap)
  {
    v2 GTerrainPos(Pos + *GTerrainMap->GetPos());
    const contentscript<glterrain>* GTerrainScript;

    for(int x = 0; x < GTerrainMap->GetSize()->X; ++x)
    {
      game::BusyAnimation();

      for(y = 0; y < GTerrainMap->GetSize()->Y; ++y)
	if(IsValidScript(GTerrainScript = GTerrainMap->GetContentScript(x, y)))
	{
	  lsquare* Square = Map[GTerrainPos.X + x][GTerrainPos.Y + y];
	  Square->ChangeGLTerrain(GTerrainScript->Instantiate());

	  if(GTerrainScript->IsInside())
	  {
	    if(*GTerrainScript->IsInside())
	      Square->Flags |= INSIDE;
	    else
	      Square->Flags &= ~INSIDE;
	  }
	}
    }
  }

  const olterraincontentmap* OTerrainMap = RoomScript->GetOTerrainMap();

  if(OTerrainMap)
  {
    v2 OTerrainPos(Pos + *OTerrainMap->GetPos());
    const contentscript<olterrain>* OTerrainScript;

    for(int x = 0; x < OTerrainMap->GetSize()->X; ++x)
    {
      game::BusyAnimation();

      for(y = 0; y < OTerrainMap->GetSize()->Y; ++y)
	if(IsValidScript(OTerrainScript = OTerrainMap->GetContentScript(x, y)))
	{
	  olterrain* Terrain = OTerrainScript->Instantiate();
	  Map[OTerrainPos.X + x][OTerrainPos.Y + y]->ChangeOLTerrain(Terrain);
	}
    }
  }

  const std::list<squarescript> Square = RoomScript->GetSquare();

  for(std::list<squarescript>::const_iterator i = Square.begin(); i != Square.end(); ++i)
  {
    game::BusyAnimation();
    const squarescript* Script = &*i;
    const interval* ScriptTimes = Script->GetTimes();
    int Times = ScriptTimes ? ScriptTimes->Randomize() : 1;

    for(int t = 0; t < Times; ++t)
    {
      v2 SquarePos;

      if(Script->GetPosition()->GetRandom())
      {
	const rect* ScriptBorders = Script->GetPosition()->GetBorders();
	rect Borders = ScriptBorders ? *ScriptBorders + Pos : rect(Pos, Pos + Size - v2(1, 1));
	SquarePos = GetRandomSquare(0, Script->GetPosition()->GetFlags(), &Borders);
      }
      else
	SquarePos = Pos + Script->GetPosition()->GetVector();

      Map[SquarePos.X][SquarePos.Y]->ApplyScript(Script, RoomClass);
    }
  }

  return true;
}

truth level::GenerateLanterns(int X, int Y, int SquarePos) const
{
  if(!(RAND() % 7))
  {
    lantern* Lantern = lantern::Spawn();
    Lantern->SignalSquarePositionChange(SquarePos);
    Map[X][Y]->GetStack()->AddItem(Lantern);
    return true;
  }

  return false;
}

void level::CreateRoomSquare(glterrain* GLTerrain, olterrain* OLTerrain, int X, int Y, int Room, int Flags) const
{
  Map[X][Y]->ChangeLTerrain(GLTerrain, OLTerrain);
  FlagMap[X][Y] |= FORBIDDEN;
  Map[X][Y]->SetRoomIndex(Room);
  Map[X][Y]->AddFlags(Flags);
}

void level::GenerateMonsters()
{
  if(*LevelScript->GenerateMonsters()
     && game::GetTeam(MONSTER_TEAM)->GetEnabledMembers() < IdealPopulation
     && (MonsterGenerationInterval <= 1 || !RAND_N(MonsterGenerationInterval)))
  {
    GenerateNewMonsters(1);
    ++MonsterGenerationInterval;
  }
}

void level::Save(outputfile& SaveFile) const
{
  area::Save(SaveFile);
  SaveFile << Room << GlobalRainLiquid << GlobalRainSpeed;

  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
      Map[x][y]->Save(SaveFile);

  SaveFile << Door << LevelMessage << IdealPopulation << MonsterGenerationInterval << Difficulty;
  SaveFile << SunLightEmitation << SunLightDirection << AmbientLuminance << NightAmbientLuminance;
  SaveFile << FirstEntryInitDone;
  SaveFile << WeatherState << WeatherTimer << WeatherRandomState;
}

void level::Load(inputfile& SaveFile)
{
  game::SetIsGenerating(true);
  game::SetIsLoading(true);
  area::Load(SaveFile);
  Map = reinterpret_cast<lsquare***>(area::Map);
  SaveFile >> Room;
  GlobalRainLiquid = static_cast<liquid*>(ReadType<material*>(SaveFile));
  SaveFile >> GlobalRainSpeed;

  if(GlobalRainLiquid)
    GlobalRainLiquid->SetVolumeNoSignals(0);

  game::SetGlobalRainLiquid(GlobalRainLiquid);
  game::SetGlobalRainSpeed(GlobalRainSpeed);
  int x, y;

  for(x = 0; x < XSize; ++x)
    for(y = 0; y < YSize; ++y)
      Map[x][y] = new lsquare(this, v2(x, y));

  for(x = 0; x < XSize; ++x)
    for(y = 0; y < YSize; ++y)
    {
      game::SetSquareInLoad(Map[x][y]);
      Map[x][y]->Load(SaveFile);
      Map[x][y]->CalculateNeighbourLSquares();
    }

  SaveFile >> Door >> LevelMessage >> IdealPopulation >> MonsterGenerationInterval >> Difficulty;
  SaveFile >> SunLightEmitation >> SunLightDirection >> AmbientLuminance >> NightAmbientLuminance;
  SaveFile >> FirstEntryInitDone;
  SaveFile >> WeatherState >> WeatherTimer >> WeatherRandomState;
  Alloc2D(NodeMap, XSize, YSize);
  Alloc2D(WalkabilityMap, XSize, YSize);

  for(x = 0; x < XSize; ++x)
    for(y = 0; y < YSize; ++y)
    {
      if(!Map[x][y]->IsInside())
	Map[x][y]->AmbientLuminance = AmbientLuminance;

      NodeMap[x][y] = new node(x, y, Map[x][y]);
      WalkabilityMap[x][y] = Map[x][y]->GetTheoreticalWalkability();
      Map[x][y]->CalculateGroundBorderPartners();
      Map[x][y]->CalculateOverBorderPartners();
    }

  SquareStack = new lsquare*[XSizeTimesYSize];
  game::SetIsLoading(false);
  game::SetIsGenerating(false);
}

void level::FiatLux()
{
  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
    {
      Map[x][y]->CalculateEmitation();
      Map[x][y]->Emitate();
      Map[x][y]->CalculateLuminance();
    }

  CheckSunLight();
}

void level::GenerateNewMonsters(int HowMany, truth ConsiderPlayer)
{
  v2 Pos;

  for(int c1 = 0; c1 < HowMany; ++c1)
  {
    character* Char = protosystem::BalancedCreateMonster();
    Char->CalculateEnchantments();

    for(int c2 = 0; c2 < 30; ++c2)
    {
      Pos = GetRandomSquare(Char);

      if(Pos == ERROR_V2)
	break;

      lsquare* Square = GetLSquare(Pos);

      if((!Square->GetRoomIndex()
	  || !Square->GetRoom()->DontGenerateMonsters())
	 && (!ConsiderPlayer
	     || (Pos - PLAYER->GetPos()).GetManhattanLength() > 6))
	break;
    }

    if(Pos != ERROR_V2)
    {
      Char->PutTo(Pos);
      Char->SetGenerationDanger(Difficulty);
      Char->SignalGeneration();
      Char->SignalNaturalGeneration();
      ivantime Time;
      game::GetTime(Time);
      int Modifier = Time.Day - EDIT_ATTRIBUTE_DAY_MIN;

      if(Modifier > 0)
	Char->EditAllAttributes(Modifier >> EDIT_ATTRIBUTE_DAY_SHIFT);
    }
    else
      delete Char;
  }
}

/* Example of the usage: GetRandomSquare() gives out a random walkable square */

v2 level::GetRandomSquare(const character* Char, int Flags, const rect* Borders) const
{
  rect LocalBorder;

  if(Borders)
  {
    LocalBorder = *Borders;
    Borders = &LocalBorder;
    LimitRef(LocalBorder.X1, 0, XSize - 1);
    LimitRef(LocalBorder.X2, 0, XSize - 1);
    LimitRef(LocalBorder.Y1, 0, YSize - 1);
    LimitRef(LocalBorder.Y2, 0, YSize - 1);
  }

  lsquare* LSquare;

  for(int c = 0;; ++c)
  {
    if(c == 50)
      Char = 0;

    if(c == 500)
      return ERROR_V2;

    v2 Pos;

    if(Borders)
    {
      Pos.X = Borders->X1 + RAND() % (Borders->X2 - Borders->X1 + 1);
      Pos.Y = Borders->Y1 + RAND() % (Borders->Y2 - Borders->Y1 + 1);
    }
    else
    {
      Pos.X = 1 + RAND() % (XSize - 2);
      Pos.Y = 1 + RAND() % (YSize - 2);
    }

    LSquare = Map[Pos.X][Pos.Y];

    if(((Char ? !!Char->CanMoveOn(LSquare) : (LSquare->GetWalkability() & WALK)) != !(Flags & NOT_WALKABLE))
       || ((Char ? !!Char->IsFreeForMe(LSquare) : !LSquare->GetCharacter()) != !(Flags & HAS_CHARACTER))
       || (Flags & ATTACHABLE && FlagMap[Pos.X][Pos.Y] & FORBIDDEN)
       || (Flags & HAS_NO_OTERRAIN && LSquare->GetOTerrain()))
      continue;

    int RoomFlags = Flags & (IN_ROOM|NOT_IN_ROOM);

    if((RoomFlags == IN_ROOM && !LSquare->GetRoomIndex())
       || (RoomFlags == NOT_IN_ROOM && LSquare->GetRoomIndex()))
      continue;

    return Pos;
  }
}

void level::ParticleTrail(v2 StartPos, v2 EndPos)
{
  if(StartPos.X != EndPos.X && StartPos.Y != EndPos.Y)
    ABORT("666th rule of thermodynamics - Particles don't move the way you want them to move.");
}

truth level::IsOnGround() const
{
  return *LevelScript->IsOnGround();
}

int level::GetLOSModifier() const
{
  return *LevelScript->GetLOSModifier();
}

void level::AddRoom(room* NewRoom)
{
  NewRoom->SetIndex(Room.size());
  Room.push_back(NewRoom);
}

room* level::GetRoom(int I) const
{
  if(!I)
    ABORT("Access to room zero denied!");

  return Room[I];
}

void level::Explosion(character* Terrorist, const festring& DeathMsg, v2 Pos, int Strength, truth HurtNeutrals)
{
  static int StrengthLimit[6] = { 500, 250, 100, 50, 25, 10 };
  uint c;
  int Size = 6;

  for(c = 0; c < 6; ++c)
    if(Strength >= StrengthLimit[c])
    {
      Size = c;
      break;
    }

  PlayerHurt.resize(PlayerHurt.size() + 1);
  explosion* Exp = new explosion;
  Exp->Terrorist = Terrorist;
  Exp->DeathMsg = DeathMsg;
  Exp->Pos = Pos;
  Exp->ID = NextExplosionID++;
  Exp->Strength = Strength;
  Exp->RadiusSquare = (8 - Size) * (8 - Size);
  Exp->Size = Size;
  Exp->HurtNeutrals = HurtNeutrals;
  ExplosionQueue.push_back(Exp);

  if(ExplosionQueue.size() == 1)
  {
    uint Explosions = 0;

    while(Explosions != ExplosionQueue.size())
    {
      for(c = Explosions; c != ExplosionQueue.size(); c = TriggerExplosions(c));
      uint NewExplosions = c;

      for(c = Explosions; c < NewExplosions; ++c)
	if(PlayerHurt[c] && PLAYER->IsEnabled())
	  PLAYER->GetHitByExplosion(ExplosionQueue[c], ExplosionQueue[c]->Strength / ((PLAYER->GetPos() - ExplosionQueue[c]->Pos).GetLengthSquare() + 1));

      Explosions = NewExplosions;
    }

    for(uint c = 0; c < ExplosionQueue.size(); ++c)
      delete ExplosionQueue[c];

    ExplosionQueue.clear();
    PlayerHurt.clear();
    NextExplosionID = 1;

    for(int x = 0; x < XSize; ++x)
      for(int y = 0; y < YSize; ++y)
	Map[x][y]->LastExplosionID = 0;
  }
}

truth level::DrawExplosion(const explosion* Explosion) const
{
  static v2 StrengthPicPos[7] = { v2(176, 176), v2(0, 144), v2(256, 32), v2(144, 32), v2(64, 32), v2(16, 32),v2(0, 32) };
  v2 BPos = game::CalculateScreenCoordinates(Explosion->Pos) - v2((6 - Explosion->Size) << 4, (6 - Explosion->Size) << 4);
  v2 SizeVect(16 + ((6 - Explosion->Size) << 5), 16 + ((6 - Explosion->Size) << 5));
  v2 OldSizeVect = SizeVect;
  v2 PicPos = StrengthPicPos[Explosion->Size];

  if(BPos.X < 0)
  {
    if(BPos.X + SizeVect.X <= 0)
      return false;
    else
    {
      PicPos.X -= BPos.X;
      SizeVect.X += BPos.X;
      BPos.X = 0;
    }
  }

  if(BPos.Y < 0)
  {
    if(BPos.Y + SizeVect.Y <= 0)
      return false;
    else
    {
      PicPos.Y -= BPos.Y;
      SizeVect.Y += BPos.Y;
      BPos.Y = 0;
    }
  }

  if(BPos.X >= RES.X || BPos.Y >= RES.Y)
    return false;

  if(BPos.X + SizeVect.X > RES.X)
    SizeVect.X = RES.X - BPos.X;

  if(BPos.Y + SizeVect.Y > RES.Y)
    SizeVect.Y = RES.Y - BPos.Y;

  int Flags = RAND() & 7;
  blitdata BlitData = { 0,
			{ PicPos.X, PicPos.Y },
			{ 0, 0 },
			{ SizeVect.X, SizeVect.Y },
			{ 0 },
			TRANSPARENT_COLOR,
			0 };

  if(!Flags || SizeVect != OldSizeVect)
  {
    BlitData.Bitmap = DOUBLE_BUFFER;
    BlitData.Dest = BPos;
    BlitData.Luminance = ivanconfig::GetContrastLuminance();
    igraph::GetSymbolGraphic()->LuminanceMaskedBlit(BlitData);
  }
  else
  {
    /* Cache these */
    bitmap ExplosionPic(SizeVect);
    ExplosionPic.ActivateFastFlag();
    BlitData.Bitmap = &ExplosionPic;
    BlitData.Flags = Flags;
    igraph::GetSymbolGraphic()->NormalBlit(BlitData);
    BlitData.Bitmap = DOUBLE_BUFFER;
    BlitData.Dest = BPos;
    BlitData.Src.X = BlitData.Src.Y = 0;
    BlitData.Luminance = ivanconfig::GetContrastLuminance();
    ExplosionPic.LuminanceMaskedBlit(BlitData);
  }

  return true;
}

struct explosioncontroller
{
  static truth Handler(int x, int y)
  {
    lsquare* Square = Map[x][y];
    Square->GetHitByExplosion(CurrentExplosion);
    return Square->IsFlyable();
  }
  static lsquare*** Map;
  static explosion* CurrentExplosion;
};

lsquare*** explosioncontroller::Map;
explosion* explosioncontroller::CurrentExplosion;

int level::TriggerExplosions(int MinIndex)
{
  int LastExplosion = ExplosionQueue.size();
  int NotSeen = 0;
  int c;

  for(c = MinIndex; c < LastExplosion; ++c)
  {
    int EmitChange = Min(50 + ExplosionQueue[c]->Strength, 255);
    GetLSquare(ExplosionQueue[c]->Pos)->SetTemporaryEmitation(MakeRGB24(EmitChange, EmitChange, EmitChange));

    if(!GetSquare(ExplosionQueue[c]->Pos)->CanBeSeenByPlayer(true))
      ++NotSeen;
  }

  if(NotSeen)
  {
    if(NotSeen == 1)
      ADD_MESSAGE("You hear an explosion.");
    else
      ADD_MESSAGE("You hear explosions.");
  }

  game::DrawEverythingNoBlit();
  truth Drawn = false;

  for(c = MinIndex; c < LastExplosion; ++c)
  {
    if(DrawExplosion(ExplosionQueue[c]))
      Drawn = true;
  }

  if(Drawn)
  {
    graphics::BlitDBToScreen();
    game::GetCurrentArea()->SendNewDrawRequest();
    clock_t StartTime = clock();
    while(clock() - StartTime < 0.3 * CLOCKS_PER_SEC);
  }

  for(c = MinIndex; c < LastExplosion; ++c)
  {
    explosion* Explosion = ExplosionQueue[c];
    int Radius = 8 - Explosion->Size;
    game::SetPlayerWasHurtByExplosion(false);
    explosioncontroller::Map = Map;
    explosioncontroller::CurrentExplosion = Explosion;

    rect Rect;
    femath::CalculateEnvironmentRectangle(Rect, GetBorder(), Explosion->Pos, Radius);

    for(int x = Rect.X1; x <= Rect.X2; ++x)
    {
      mapmath<explosioncontroller>::DoLine(Explosion->Pos.X, Explosion->Pos.Y, x, Rect.Y1);
      mapmath<explosioncontroller>::DoLine(Explosion->Pos.X, Explosion->Pos.Y, x, Rect.Y2);
    }

    for(int y = Rect.Y1 + 1; y < Rect.Y2; ++y)
    {
      mapmath<explosioncontroller>::DoLine(Explosion->Pos.X, Explosion->Pos.Y, Rect.X1, y);
      mapmath<explosioncontroller>::DoLine(Explosion->Pos.X, Explosion->Pos.Y, Rect.X2, y);
    }

    PlayerHurt[c] = game::PlayerWasHurtByExplosion();

    if(GetLSquare(Explosion->Pos)->IsFlyable())
      GetLSquare(Explosion->Pos)->AddSmoke(gas::Spawn(SMOKE, 1000));
  }

  for(c = MinIndex; c < LastExplosion; ++c)
    GetLSquare(ExplosionQueue[c]->Pos)->SetTemporaryEmitation(0);

  return LastExplosion;
}

truth level::CollectCreatures(charactervector& CharacterArray, character* Leader,
			      truth AllowHostiles, std::vector<v2>* Positions)
{
  int c;

  if(!AllowHostiles)
    for(c = 0; c < game::GetTeams(); ++c)
      if(Leader->GetTeam()->GetRelation(game::GetTeam(c)) == HOSTILE)
	for(std::list<character*>::const_iterator i = game::GetTeam(c)->GetMember().begin(); i != game::GetTeam(c)->GetMember().end(); ++i)
	  if((*i)->IsEnabled() && Leader->CanBeSeenBy(*i)
	     && Leader->SquareUnderCanBeSeenBy(*i, true) && (*i)->CanFollow())
	  {
	    ADD_MESSAGE("You can't escape when there are hostile creatures nearby.");
	    return false;
	  }

  truth TakeAll = true;

  for(c = 0; c < game::GetTeams(); ++c)
    if(game::GetTeam(c)->GetEnabledMembers()
       && Leader->GetTeam()->GetRelation(game::GetTeam(c)) == HOSTILE)
    {
      TakeAll = false;
      break;
    }

  for(c = 0; c < game::GetTeams(); ++c)
    if(game::GetTeam(c) == Leader->GetTeam() || Leader->GetTeam()->GetRelation(game::GetTeam(c)) == HOSTILE)
      for(std::list<character*>::const_iterator i = game::GetTeam(c)->GetMember().begin(); i != game::GetTeam(c)->GetMember().end(); ++i)
	/* Only characters standing on this very level can be taken along. A
	   team member without a square (a world-map group companion, or one
	   already detached) has no source position to record and must not be
	   reached for one. */
	if((*i)->IsEnabled() && *i != Leader && (*i)->GetSquareUnder()
	   && (TakeAll
	       || (Leader->CanBeSeenBy(*i)
		   && Leader->SquareUnderCanBeSeenBy(*i, true)))
	   && (*i)->CanFollow()
	   && (*i)->GetCommandFlags() & FOLLOW_LEADER)
	{
	  if((*i)->GetAction() && (*i)->GetAction()->IsVoluntary())
	    (*i)->GetAction()->Terminate(false);

	  if(!(*i)->GetAction())
	  {
	    ADD_MESSAGE("%s follows you.", (*i)->CHAR_NAME(DEFINITE));

	    /* Remember where each member stood so an aborted transfer can put
	       it back exactly where it was. */
	    if(Positions)
	      Positions->push_back((*i)->GetPos());

	    CharacterArray.push_back(*i);
	    (*i)->Remove();
	  }
	}

  return true;
}

void level::Draw(truth AnimationDraw) const
{
  const int XMin = Max(game::GetCamera().X, 0);
  const int YMin = Max(game::GetCamera().Y, 0);
  const int XMax = Min(XSize, game::GetCamera().X + game::GetScreenXSize());
  const int YMax = Min(YSize, game::GetCamera().Y + game::GetScreenYSize());
  const ulong LOSTick = game::GetLOSTick();
  blitdata BlitData = { DOUBLE_BUFFER,
			{ 0, 0 },
			{ 0, 0 },
			{ TILE_SIZE, TILE_SIZE },
			{ 0 },
			TRANSPARENT_COLOR,
			ALLOW_ANIMATE|ALLOW_ALPHA };

  if(!game::GetSeeWholeMapCheatMode())
  {
    if(!AnimationDraw)
    {
      for(int x = XMin; x < XMax; ++x)
      {
	BlitData.Dest = game::CalculateScreenCoordinates(v2(x, YMin));
	lsquare** SquarePtr = &Map[x][YMin];

	for(int y = YMin; y < YMax; ++y, ++SquarePtr, BlitData.Dest.Y += TILE_SIZE)
	{
	  const lsquare* Square = *SquarePtr;
	  const ulong LastSeen = Square->LastSeen;

	  if(LastSeen == LOSTick)
	    Square->Draw(BlitData);
	  else if(Square->Flags & STRONG_BIT || LastSeen == LOSTick - 2)
	    Square->DrawMemorized(BlitData);
	}
      }
    }
    else
    {
      for(int x = XMin; x < XMax; ++x)
      {
	BlitData.Dest = game::CalculateScreenCoordinates(v2(x, YMin));
	lsquare** SquarePtr = &Map[x][YMin];

	for(int y = YMin; y < YMax; ++y, ++SquarePtr, BlitData.Dest.Y += TILE_SIZE)
	{
	  const lsquare* Square = *SquarePtr;

	  if(Square->LastSeen == LOSTick)
	    Square->Draw(BlitData);
	  else
	  {
	    const character* C = Square->Character;

	    if(C && C->CanBeSeenByPlayer())
	      Square->DrawMemorizedCharacter(BlitData);
	  }
	}
      }
    }
  }
  else
  {
    for(int x = XMin; x < XMax; ++x)
    {
      BlitData.Dest = game::CalculateScreenCoordinates(v2(x, YMin));
      lsquare** SquarePtr = &Map[x][YMin];

      for(int y = YMin; y < YMax; ++y, ++SquarePtr, BlitData.Dest.Y += TILE_SIZE)
	(*SquarePtr)->Draw(BlitData);
    }
  }
}

v2 level::GetEntryPos(const character* Char, int I) const
{
  if(I == FOUNTAIN)
  {
    std::vector<v2> Fountains;
    for(int x = 0; x < XSize; ++x)
      for(int y = 0; y < YSize; ++y)
      {
	if(GetLSquare(x,y)->GetOLTerrain() && GetLSquare(x,y)->GetOLTerrain()->IsFountainWithWater())
	  Fountains.push_back(v2(x,y));
      }

    if(Fountains.empty())
      return GetRandomSquare();

    return Fountains[RAND_N(Fountains.size())];
  }
  std::map<int, v2>::const_iterator i = EntryMap.find(I);
  return i == EntryMap.end() ? GetRandomSquare(Char) : i->second;
}

void level::GenerateRectangularRoom(std::vector<v2>& OKForDoor, std::vector<v2>& Inside, std::vector<v2>& Border, const roomscript* RoomScript, room* RoomClass, v2 Pos, v2 Size)
{
  const contentscript<glterrain>* GTerrain;
  const contentscript<olterrain>* OTerrain;

  if(*RoomScript->UseFillSquareWalls())
  {
    GTerrain = LevelScript->GetFillSquare()->GetGTerrain();
    OTerrain = LevelScript->GetFillSquare()->GetOTerrain();
  }
  else
  {
    GTerrain = RoomScript->GetWallSquare()->GetGTerrain();
    OTerrain = RoomScript->GetWallSquare()->GetOTerrain();
  }

  int Room = RoomClass->GetIndex();
  truth AllowLanterns = *RoomScript->GenerateLanterns();
  truth AllowWindows = *RoomScript->GenerateWindows();
  int x, y;
  int Shape = *RoomScript->GetShape();
  int Flags = (GTerrain->IsInside() ? *GTerrain->IsInside() : *RoomScript->IsInside()) ? INSIDE : 0;

  if(Shape == ROUND_CORNERS && (Size.X < 5 || Size.Y < 5)) /* No weird shapes this way. */
    Shape = RECTANGLE;

  for(x = Pos.X; x < Pos.X + Size.X; ++x)
  {
    if(Shape == ROUND_CORNERS)
    {
      if(x == Pos.X)
      {
	CreateRoomSquare(GTerrain->Instantiate(), OTerrain->Instantiate(), x + 1, Pos.Y + 1, Room, Flags);
	CreateRoomSquare(GTerrain->Instantiate(), OTerrain->Instantiate(), x + 1, Pos.Y + Size.Y - 2, Room, Flags);
	Border.push_back(v2(x + 1, Pos.Y + 1));
	Border.push_back(v2(x + 1, Pos.Y + Size.Y - 2));
	continue;
      }
      else if(x == Pos.X + Size.X - 1)
      {
	CreateRoomSquare(GTerrain->Instantiate(), OTerrain->Instantiate(), x - 1, Pos.Y + 1, Room, Flags);
	CreateRoomSquare(GTerrain->Instantiate(), OTerrain->Instantiate(), x - 1, Pos.Y + Size.Y - 2, Room, Flags);
	Border.push_back(v2(x - 1, Pos.Y + 1));
	Border.push_back(v2(x - 1, Pos.Y + Size.Y - 2));
	continue;
      }
    }

    CreateRoomSquare(GTerrain->Instantiate(), OTerrain->Instantiate(), x, Pos.Y, Room, Flags);
    CreateRoomSquare(GTerrain->Instantiate(), OTerrain->Instantiate(), x, Pos.Y + Size.Y - 1, Room, Flags);

    if((Shape == RECTANGLE && x != Pos.X && x != Pos.X + Size.X - 1)
       || (Shape == ROUND_CORNERS && x > Pos.X + 1 && x < Pos.X + Size.X - 2))
    {
      OKForDoor.push_back(v2(x, Pos.Y));
      OKForDoor.push_back(v2(x, Pos.Y + Size.Y - 1));

      if((!AllowLanterns || !GenerateLanterns(x, Pos.Y, DOWN)) && AllowWindows)
	GenerateWindows(x, Pos.Y);

      if((!AllowLanterns || !GenerateLanterns(x, Pos.Y + Size.Y - 1, UP)) && AllowWindows)
	GenerateWindows(x, Pos.Y + Size.Y - 1);
    }

    Border.push_back(v2(x, Pos.Y));
    Border.push_back(v2(x, Pos.Y + Size.Y - 1));
  }

  game::BusyAnimation();

  for(y = Pos.Y + 1; y < Pos.Y + Size.Y - 1; ++y)
  {
    CreateRoomSquare(GTerrain->Instantiate(), OTerrain->Instantiate(), Pos.X, y, Room, Flags);
    CreateRoomSquare(GTerrain->Instantiate(), OTerrain->Instantiate(), Pos.X + Size.X - 1, y, Room, Flags);

    if(Shape == RECTANGLE || (Shape == ROUND_CORNERS && y != Pos.Y + 1 && y != Pos.Y + Size.Y - 2))
    {
      OKForDoor.push_back(v2(Pos.X, y));
      OKForDoor.push_back(v2(Pos.X + Size.X - 1, y));

      if((!AllowLanterns || !GenerateLanterns(Pos.X, y, RIGHT)) && AllowWindows)
	GenerateWindows(Pos.X, y);

      if((!AllowLanterns || !GenerateLanterns(Pos.X + Size.X - 1, y, LEFT)) && AllowWindows)
	GenerateWindows(Pos.X + Size.X - 1, y);
    }

    Border.push_back(v2(Pos.X, y));
    Border.push_back(v2(Pos.X + Size.X - 1, y));
  }

  GTerrain = RoomScript->GetFloorSquare()->GetGTerrain();
  OTerrain = RoomScript->GetFloorSquare()->GetOTerrain();
  Flags = (GTerrain->IsInside() ? *GTerrain->IsInside() : *RoomScript->IsInside()) ? INSIDE : 0;

  for(x = Pos.X + 1; x < Pos.X + Size.X - 1; ++x)
    for(y = Pos.Y + 1; y < Pos.Y + Size.Y - 1; ++y)
    {
      /* if not in the corner */

      if(!(Shape == ROUND_CORNERS && (x == Pos.X + 1 || x == Pos.X + Size.X - 2) && (y == Pos.Y + 1 || y == Pos.Y + Size.Y - 2)))
      {
	CreateRoomSquare(GTerrain->Instantiate(), OTerrain->Instantiate(), x, y, Room, Flags);
	Inside.push_back(v2(x,y));
      }
    }
}

void level::Reveal()
{
  ulong Tick = game::GetLOSTick();

  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
      Map[x][y]->Reveal(Tick);
}

void level::ParticleBeam(beamdata& Beam)
{
  v2 CurrentPos = Beam.StartPos;

  if(Beam.Direction != YOURSELF)
  {
    for(int Length = 0; Length < Beam.Range; ++Length)
    {
      CurrentPos += game::GetMoveVector(Beam.Direction);

      if(!IsValidPos(CurrentPos))
	break;

      lsquare* CurrentSquare = GetLSquare(CurrentPos);

      if(!CurrentSquare->IsFlyable())
      {
	(CurrentSquare->*lsquare::GetBeamEffect(Beam.BeamEffect))(Beam);
	break;
      }
      else
      {
	CurrentSquare->DrawParticles(Beam.BeamColor);

	if((CurrentSquare->*lsquare::GetBeamEffect(Beam.BeamEffect))(Beam))
	  break;
      }
    }
  }
  else
  {
    lsquare* Where = GetLSquare(CurrentPos);
    Where->DrawParticles(Beam.BeamColor);
    (Where->*lsquare::GetBeamEffect(Beam.BeamEffect))(Beam);
  }
}

/* Note: You will most likely need some help from supernatural entities to comprehend this code. Sorry. */

void level::LightningBeam(beamdata& Beam)
{
  v2 CurrentPos = Beam.StartPos;

  if(Beam.Direction == YOURSELF)
  {
    lsquare* Where = GetLSquare(CurrentPos);

    for(int c = 0; c < 4; ++c)
      Where->DrawLightning(v2(8, 8), Beam.BeamColor, YOURSELF);

    (Where->*lsquare::GetBeamEffect(Beam.BeamEffect))(Beam);
    return;
  }

  v2 StartPos;

  switch(Beam.Direction)
  {
   case 0: StartPos = v2(15, 15); break;
   case 1: StartPos = v2(RAND() & 15, 15); break;
   case 2: StartPos = v2(0, 15); break;
   case 3: StartPos = v2(15, RAND() & 15); break;
   case 4: StartPos = v2(0, RAND() & 15); break;
   case 5: StartPos = v2(15, 0); break;
   case 6: StartPos = v2(RAND() & 15, 0); break;
   case 7: StartPos = v2(0, 0); break;
  }

  for(int Length = 0; Length < Beam.Range; ++Length)
  {
    CurrentPos += game::GetMoveVector(Beam.Direction);

    if(!IsValidPos(CurrentPos))
      break;

    lsquare* CurrentSquare = GetLSquare(CurrentPos);

    if(!CurrentSquare->IsFlyable())
    {
      if((CurrentSquare->*lsquare::GetBeamEffect(Beam.BeamEffect))(Beam))
	break;

      truth W1, W2;

      switch(Beam.Direction)
      {
       case 0:
	W1 = GetLSquare(CurrentPos + v2(1, 0))->IsFlyable();
	W2 = GetLSquare(CurrentPos + v2(0, 1))->IsFlyable();

	if(W1 == W2)
	  Beam.Direction = 7;
	else if(W1)
	{
	  ++CurrentPos.Y;
	  Beam.Direction = 2;
	}
	else
	{
	  ++CurrentPos.X;
	  Beam.Direction = 5;
	}

	break;
       case 1: Beam.Direction = 6; StartPos.Y = 0; break;
       case 2:
	W1 = GetLSquare(CurrentPos + v2(-1, 0))->IsFlyable();
	W2 = GetLSquare(CurrentPos + v2(0, 1))->IsFlyable();

	if(W1 == W2)
	  Beam.Direction = 5;
	else if(W1)
	{
	  ++CurrentPos.Y;
	  Beam.Direction = 0;
	}
	else
	{
	  --CurrentPos.X;
	  Beam.Direction = 7;
	}

	break;
       case 3: Beam.Direction = 4; StartPos.X = 0; break;
       case 4: Beam.Direction = 3; StartPos.X = 15; break;
       case 5:
	W1 = GetLSquare(CurrentPos + v2(1, 0))->IsFlyable();
	W2 = GetLSquare(CurrentPos + v2(0, -1))->IsFlyable();

	if(W1 == W2)
	  Beam.Direction = 2;
	else if(W1)
	{
	  --CurrentPos.Y;
	  Beam.Direction = 7;
	}
	else
	{
	  ++CurrentPos.X;
	  Beam.Direction = 0;
	}

	break;
       case 6: Beam.Direction = 1; StartPos.Y = 15; break;
       case 7:
	W1 = GetLSquare(CurrentPos + v2(-1, 0))->IsFlyable();
	W2 = GetLSquare(CurrentPos + v2(0, -1))->IsFlyable();

	if(W1 == W2)
	  Beam.Direction = 0;
	else if(W1)
	{
	  --CurrentPos.Y;
	  Beam.Direction = 5;
	}
	else
	{
	  --CurrentPos.X;
	  Beam.Direction = 2;
	}

	break;
      }

      switch(Beam.Direction)
      {
       case 0: StartPos = v2(15, 15); break;
       case 2: StartPos = v2(0, 15); break;
       case 5: StartPos = v2(15, 0); break;
       case 7: StartPos = v2(0, 0); break;
      }
    }
    else
    {
      StartPos = CurrentSquare->DrawLightning(StartPos, Beam.BeamColor, Beam.Direction);

      if((CurrentSquare->*lsquare::GetBeamEffect(Beam.BeamEffect))(Beam))
	break;
    }
  }
}

void level::ShieldBeam(beamdata& Beam)
{
  v2 Pos[3];

  switch(Beam.Direction)
  {
   case 0:
    Pos[0] = v2(-1, 0);
    Pos[1] = v2(-1, -1);
    Pos[2] = v2(0, -1);
    break;
   case 1:
    Pos[0] = v2(-1, -1);
    Pos[1] = v2(0, -1);
    Pos[2] = v2(1, -1);
    break;
   case 2:
    Pos[0] = v2(0, -1);
    Pos[1] = v2(1, -1);
    Pos[2] = v2(1, 0);
    break;
   case 3:
    Pos[0] = v2(-1, 1);
    Pos[1] = v2(-1, 0);
    Pos[2] = v2(-1, -1);
    break;
   case 4:
    Pos[0] = v2(1, -1);
    Pos[1] = v2(1, 0);
    Pos[2] = v2(1, 1);
    break;
   case 5:
    Pos[0] = v2(0, 1);
    Pos[1] = v2(-1, 1);
    Pos[2] = v2(-1, 0);
    break;
   case 6:
    Pos[0] = v2(1, 1);
    Pos[1] = v2(0, 1);
    Pos[2] = v2(-1, 1);
    break;
   case 7:
    Pos[0] = v2(1, 0);
    Pos[1] = v2(1, 1);
    Pos[2] = v2(0, 1);
    break;
   case 8:
    GetLSquare(Beam.StartPos)->DrawParticles(Beam.BeamColor);
    (GetLSquare(Beam.StartPos)->*lsquare::GetBeamEffect(Beam.BeamEffect))(Beam);
    return;
  }

  for(int c = 0; c < 3; ++c)
    if(IsValidPos(Beam.StartPos + Pos[c]))
    {
      GetLSquare(Beam.StartPos + Pos[c])->DrawParticles(Beam.BeamColor);
      (GetLSquare(Beam.StartPos + Pos[c])->*lsquare::GetBeamEffect(Beam.BeamEffect))(Beam);
    }
}

outputfile& operator<<(outputfile& SaveFile, const level* Level)
{
  Level->Save(SaveFile);
  return SaveFile;
}

inputfile& operator>>(inputfile& SaveFile, level*& Level)
{
  Level = new level;
  Level->Load(SaveFile);
  return SaveFile;
}

void (level::*Beam[BEAM_STYLES])(beamdata&) =
{
  &level::ParticleBeam,
  &level::LightningBeam,
  &level::ShieldBeam
};

void (level::*level::GetBeam(int I))(beamdata&)
{
  return Beam[I];
}

v2 level::FreeSquareSeeker(const character* Char, v2 StartPos, v2 Prohibited, int MaxDistance, truth AllowStartPos) const
{
  int c;

  for(c = 0; c < 8; ++c)
  {
    v2 Pos = StartPos + game::GetMoveVector(c);

    if(IsValidPos(Pos) && Char->CanMoveOn(GetLSquare(Pos)) && Char->IsFreeForMe(GetLSquare(Pos)) && Pos != Prohibited && (AllowStartPos || !Char->PlaceIsIllegal(Pos, Prohibited)))
      return Pos;
  }

  if(MaxDistance)
    for(c = 0; c < 8; ++c)
    {
      v2 Pos = StartPos + game::GetMoveVector(c);

      if(IsValidPos(Pos))
      {
	if(Char->CanMoveOn(GetLSquare(Pos)) && Pos != Prohibited)
	{
	  Pos = FreeSquareSeeker(Char, Pos, Prohibited, MaxDistance - 1, AllowStartPos);

	  if(Pos != ERROR_V2)
	    return Pos;
	}
      }
    }

  return ERROR_V2;
}

/* Returns ERROR_V2 if no free square was found */

v2 level::GetNearestFreeSquare(const character* Char, v2 StartPos, truth AllowStartPos) const
{
  if(AllowStartPos && Char->CanMoveOn(GetLSquare(StartPos)) && Char->IsFreeForMe(GetLSquare(StartPos)))
    return StartPos;

  int c;

  for(c = 0; c < 8; ++c)
  {
    v2 Pos = StartPos + game::GetMoveVector(c);

    if(IsValidPos(Pos) && Char->CanMoveOn(GetLSquare(Pos)) && Char->IsFreeForMe(GetLSquare(Pos)) && (AllowStartPos || !Char->PlaceIsIllegal(Pos, StartPos)))
      return Pos;
  }

  for(int Dist = 0; Dist < 5; ++Dist)
    for(c = 0; c < 8; ++c)
    {
      v2 Pos = StartPos + game::GetMoveVector(c);

      if(IsValidPos(Pos) && Char->CanMoveOn(GetLSquare(Pos)))
      {
	Pos = FreeSquareSeeker(Char, Pos, StartPos, Dist, AllowStartPos);

	if(Pos != ERROR_V2)
	  return Pos;
      }
    }

  return ERROR_V2;
}

v2 level::GetFreeAdjacentSquare(const character* Char, v2 StartPos, truth AllowCharacter) const
{
  int PossibleDir[8];
  int Index = 0;
  lsquare* Origo = GetLSquare(StartPos);

  for(int d = 0; d < 8; ++d)
  {
    lsquare* Square = Origo->GetNeighbourLSquare(d);

    if(Square && Char->CanMoveOn(Square) && (AllowCharacter || Char->IsFreeForMe(Square)))
      PossibleDir[Index++] = d;
  }

  return Index ? StartPos + game::GetMoveVector(PossibleDir[RAND() % Index]) : ERROR_V2;
}

void (level::*level::GetBeamEffectVisualizer(int I))(const fearray<lsquare*>&, col16) const
{
  static void (level::*Visualizer[BEAM_STYLES])(const fearray<lsquare*>&, col16) const = { &level::ParticleVisualizer, &level::LightningVisualizer, &level::ParticleVisualizer };
  return Visualizer[I];
}

void level::ParticleVisualizer(const fearray<lsquare*>& Stack, col16 BeamColor) const
{
  clock_t StartTime = clock();
  game::DrawEverythingNoBlit();

  for(fearray<lsquare*>::sizetype c = 0; c < Stack.Size; ++c)
    Stack[c]->DrawParticles(BeamColor, false);

  graphics::BlitDBToScreen();
  while(clock() - StartTime < 0.05 * CLOCKS_PER_SEC);
}

void level::LightningVisualizer(const fearray<lsquare*>& Stack, col16 BeamColor) const
{
  clock_t StartTime = clock();
  game::DrawEverythingNoBlit();

  for(fearray<lsquare*>::sizetype c = 0; c < Stack.Size; ++c)
    Stack[c]->DrawLightning(v2(8, 8), BeamColor, YOURSELF, false);

  graphics::BlitDBToScreen();
  while(clock() - StartTime < 0.05 * CLOCKS_PER_SEC);
}

truth level::PreProcessForBone()
{
  if(!*LevelScript->CanGenerateBone())
    return false;

  /* Gum solution */

  game::SetQuestMonstersFound(0);

  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
      Map[x][y]->PreProcessForBone();

  int DungeonIndex = GetDungeon()->GetIndex();

  return !(DungeonIndex == ELPURI_CAVE && Index == IVAN_LEVEL && game::GetQuestMonstersFound() < 5)
			 &&  (game::GetQuestMonstersFound()
			      || ((DungeonIndex != UNDER_WATER_TUNNEL || Index != VESANA_LEVEL)
				  &&  (DungeonIndex != ELPURI_CAVE || (Index != ENNER_BEAST_LEVEL && Index != DARK_LEVEL))));
}

truth level::PostProcessForBone()
{
  game::SetTooGreatDangerFound(false);
  double DangerSum = 0;
  int Enemies = 0;

  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
      Map[x][y]->PostProcessForBone(DangerSum, Enemies);

  if(game::TooGreatDangerFound() || (Enemies && DangerSum / Enemies > Difficulty * 10))
    return false;

  return true;
}

void level::FinalProcessForBone()
{
  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
      Map[x][y]->FinalProcessForBone();

  for(uint c = 1; c < Room.size(); ++c)
    Room[c]->FinalProcessForBone();
}

void level::GenerateDungeon(int Index)
{
  const festring* Msg = LevelScript->GetLevelMessage();

  if(Msg)
    LevelMessage = *Msg;

  if(*LevelScript->GenerateMonsters())
  {
    MonsterGenerationInterval = *LevelScript->GetMonsterGenerationIntervalBase() + *LevelScript->GetMonsterGenerationIntervalDelta() * Index;
    IdealPopulation = *LevelScript->GetMonsterAmountBase() + *LevelScript->GetMonsterAmountDelta() * Index;
  }

  Difficulty = 0.001 * (*LevelScript->GetDifficultyBase() + *LevelScript->GetDifficultyDelta() * Index);
  EnchantmentMinusChance = *LevelScript->GetEnchantmentMinusChanceBase() + *LevelScript->GetEnchantmentMinusChanceDelta() * Index;
  EnchantmentPlusChance = *LevelScript->GetEnchantmentPlusChanceBase() + *LevelScript->GetEnchantmentPlusChanceDelta() * Index;
  const contentscript<glterrain>* GTerrain = LevelScript->GetFillSquare()->GetGTerrain();
  const contentscript<olterrain>* OTerrain = LevelScript->GetFillSquare()->GetOTerrain();
  int x;
  game::BusyAnimation();

  for(x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
      Map[x][y]->SetLTerrain(GTerrain->Instantiate(), OTerrain->Instantiate());

  uint c;
  uint Rooms = LevelScript->GetRooms()->Randomize();
  const std::list<roomscript>& RoomList = LevelScript->GetRoom();
  std::list<roomscript>::const_iterator Iterator = RoomList.begin();

  for(c = 0; c < Rooms; ++c)
  {
    game::BusyAnimation();

    if(c < RoomList.size())
    {
      int i;

      for(i = 0; i < 1000; ++i)
	if(MakeRoom(&*Iterator))
	  break;

      if(i == 1000)
	ABORT("Failed to place special room #%d!", c);

      ++Iterator;
    }
    else
    {
      const roomscript* RoomScript = LevelScript->GetRoomDefault();

      for(int i = 0; i < 50; ++i)
	if(MakeRoom(RoomScript))
	  break;
    }
  }

  game::BusyAnimation();

  if(!*LevelScript->IgnoreDefaultSpecialSquares())
  {
    /* Gum solution */

    const levelscript* LevelBase = static_cast<const levelscript*>(LevelScript->GetBase());

    if(LevelBase)
    {
      const std::list<squarescript>& Square = LevelBase->GetSquare();

      for(std::list<squarescript>::const_iterator i = Square.begin(); i != Square.end(); ++i)
      {
	game::BusyAnimation();
	ApplyLSquareScript(&*i);
      }
    }
  }

  const std::list<squarescript>& Square = LevelScript->GetSquare();

  for(std::list<squarescript>::const_iterator i = Square.begin(); i != Square.end(); ++i)
  {
    game::BusyAnimation();
    ApplyLSquareScript(&*i);
  }

  for(c = 0; c < AttachQueue.size(); ++c)
    AttachPos(AttachQueue[c].X, AttachQueue[c].Y);

  for(x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
    {
      Map[x][y]->CalculateGroundBorderPartners();
      Map[x][y]->CalculateOverBorderPartners();
    }

  AttachQueue.clear();
  CreateItems(LevelScript->GetItems()->Randomize());
}

/* Smooth, spatially correlated pseudo-random field in 0..1, used to give
   terrain broad stands and irregular clearings instead of independent per-cell
   noise or a handful of identical circular discs. Three octaves of bilinearly
   interpolated random lattices (wide stands, medium structure, ragged edges)
   are summed with smoothstep easing. */
struct wildernessterrainnoise
{
  static const int Octaves = 3;

  void Init(int XSize, int YSize, const int* Cells)
  {
    for(int o = 0; o < Octaves; ++o)
    {
      Spacing[o] = Max(1, Cells[o]);
      GX[o] = XSize / Spacing[o] + 2;
      GY[o] = YSize / Spacing[o] + 2;
      Grid[o].assign(GX[o] * GY[o], 0.0);

      for(int i = 0; i < GX[o] * GY[o]; ++i)
	Grid[o][i] = double(RAND() % 100000) / 100000.0;
    }
  }

  double SampleOctave(int o, int X, int Y) const
  {
    int CellX = X / Spacing[o];
    int CellY = Y / Spacing[o];
    double FX = double(X % Spacing[o]) / Spacing[o];
    double FY = double(Y % Spacing[o]) / Spacing[o];
    FX = FX * FX * (3 - 2 * FX);
    FY = FY * FY * (3 - 2 * FY);
    const std::vector<double>& G = Grid[o];
    double A = G[CellY * GX[o] + CellX];
    double B = G[CellY * GX[o] + CellX + 1];
    double C = G[(CellY + 1) * GX[o] + CellX];
    double D = G[(CellY + 1) * GX[o] + CellX + 1];
    return (A * (1 - FX) + B * FX) * (1 - FY)
      + (C * (1 - FX) + D * FX) * FY;
  }

  double At(int X, int Y) const
  {
    return 0.5 * SampleOctave(0, X, Y) + 0.3 * SampleOctave(1, X, Y)
      + 0.2 * SampleOctave(2, X, Y);
  }

  int Spacing[Octaves];
  int GX[Octaves];
  int GY[Octaves];
  std::vector<double> Grid[Octaves];
};

/* The field value at a chosen percentile, so a generator can ask for a target
   coverage instead of guessing an absolute threshold that depends on the
   field's distribution. */
double WildernessCoverageThreshold(const wildernessterrainnoise& Field,
				  int XSize, int YSize, double Coverage)
{
  std::vector<double> Values;
  Values.reserve(XSize * YSize);

  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
      Values.push_back(Field.At(x, y));

  std::sort(Values.begin(), Values.end());
  int Index = int((1.0 - Coverage) * Values.size());

  if(Index < 0)
    Index = 0;

  if(Index >= int(Values.size()))
    Index = int(Values.size()) - 1;

  return Values[Index];
}

/* A coherent pool of water, clearing any decoration the cell may already
   carry. Used for the occasional wet patch rather than single-cell dots. */
void WildernessPlaceWater(lsquare*** Map, v2 Center, int Radius, int Config,
			  int XSize, int YSize)
{
  for(int dx = -Radius; dx <= Radius; ++dx)
    for(int dy = -Radius; dy <= Radius; ++dy)
    {
      v2 Pos = Center + v2(dx, dy);

      if(Pos.X < 0 || Pos.Y < 0 || Pos.X >= XSize || Pos.Y >= YSize)
	continue;

      if((Pos - Center).GetLengthSquare() > Radius * Radius)
	continue;

      Map[Pos.X][Pos.Y]->ChangeOLTerrain(0);
      Map[Pos.X][Pos.Y]->ChangeGLTerrain(liquidterrain::Spawn(Config));
    }
}

/* A small symmetric per-map variation on a scalar parameter, so neighbouring
   local areas of one biome do not all come out equally dense. */
double WildernessJitter(double HalfRange)
{
  return double(int(RAND() % 2001) - 1000) / 1000.0 * HalfRange;
}

/* Jitters the three octave scales a little so stand size varies between maps
   without letting any octave collapse to noise. */
void WildernessJitterCells(const int* Base, int* Out)
{
  for(int o = 0; o < 3; ++o)
    Out[o] = Max(2, Base[o] + int(RAND_N(3)) - 1);
}

/* Per-biome ecology parameters in one place. The generator samples a small
   per-map variant of the scalar values, so stand density, patch scale, species
   mix and ground/rock/water/snow tendencies are tuned together and can be
   checked by the diagnostic rather than scattered through the generators. */
struct wildernessbiome
{
  double TreeCoverage;      /* target fraction of the map carrying decoration */
  double CoverageJitter;    /* per-map uniform half-range on TreeCoverage */
  int Cells[3];             /* stand / structure / edge octave scales */
  int PrimarySpecies;       /* species where the species field is high */
  int SecondarySpecies;     /* species where it is low */
  int CanopySpecies;        /* opaque canopy config, 0 for none */
  int ThicketSpecies;       /* impassable thicket config, 0 for none */
  int CanopyPercent;        /* share of vegetation that becomes opaque canopy */
  int ThicketPercent;       /* share that becomes impassable thicket */
  int RockChance;           /* 1/N of vegetation cells become boulders */
  int OutcropPermille;      /* off-stand correlated rock coverage, per mille */
  int OutcropJitter;        /* per-map variation in the outcrop coverage */
  int GroundConfig;         /* base ground */
  int RichGroundConfig;     /* stand / exposed ground, 0 for none */
  int RichGroundPercent;    /* coverage of the rich or exposed ground */
  int SnowConfig;           /* snow field ground, 0 for none */
  int SnowPercent;          /* snow coverage; placed only in the clearings */
  int WaterArea;            /* squares per coherent wet feature, 0 for none */
  int WaterChance;          /* percent chance of placing each wet feature */
  int WaterMaxRadius;
  int WallPermille;         /* glacier ice-mass coverage, per mille */
  int FractureArea;         /* squares per raised fracture line, 0 for none */
  int Boulder;              /* 0: random rock; otherwise an explicit config */
};

wildernessbiome WildernessBiomeForType(int Type)
{
  wildernessbiome P = {};
  P.Cells[0] = 12;
  P.Cells[1] = 6;
  P.Cells[2] = 3;
  P.RockChance = 10;
  P.GroundConfig = GRASS_TERRAIN;
  P.WaterMaxRadius = 3;

  switch(Type)
  {
   case JUNGLE:
     P.TreeCoverage = 0.25;
     P.CoverageJitter = 0.04;
     P.Cells[0] = 11; P.Cells[1] = 6; P.Cells[2] = 3;
     P.PrimarySpecies = PALM;
     P.SecondarySpecies = TEAK;
     P.CanopySpecies = JUNGLE_CANOPY;
     P.CanopyPercent = 45;
     P.ThicketSpecies = JUNGLE_THICKET;
     P.ThicketPercent = 5;
     P.RockChance = 12;
     P.RichGroundConfig = FOREST_FLOOR;
     P.RichGroundPercent = 10;
     P.WaterArea = 1600;
     P.WaterChance = 55;
     P.WaterMaxRadius = 4;
     break;

   case LEAFY_FOREST:
     P.TreeCoverage = 0.20;
     P.CoverageJitter = 0.03;
     P.Cells[0] = 11; P.Cells[1] = 6; P.Cells[2] = 3;
     P.PrimarySpecies = OAK;
     P.SecondarySpecies = BIRCH;
     P.RockChance = 10;
     P.RichGroundConfig = FOREST_FLOOR;
     P.RichGroundPercent = 14;
     P.WaterArea = 4000;
     P.WaterChance = 25;
     break;

   case EVERGREEN_FOREST:
     P.TreeCoverage = 0.22;
     P.CoverageJitter = 0.03;
     P.Cells[0] = 10; P.Cells[1] = 5; P.Cells[2] = 3;
     P.PrimarySpecies = PINE;
     P.SecondarySpecies = FIR;
     P.RockChance = 9;
     P.RichGroundConfig = FOREST_FLOOR;
     P.RichGroundPercent = 16;
     P.SnowConfig = SNOW_TERRAIN;
     P.SnowPercent = 42;
     P.WaterArea = 4000;
     P.WaterChance = 15;
     break;

   case STEPPE:
     P.TreeCoverage = 0.005;
     P.CoverageJitter = 0.002;
     P.Cells[0] = 18; P.Cells[1] = 9; P.Cells[2] = 4;
     P.PrimarySpecies = OAK;
     P.SecondarySpecies = BIRCH;
     P.OutcropPermille = 30;
     P.OutcropJitter = 18;
     P.RichGroundConfig = DARK_GRASS_TERRAIN;
     P.RichGroundPercent = 28;
     break;

   case DESERT:
     P.Cells[0] = 16; P.Cells[1] = 8; P.Cells[2] = 4;
     P.OutcropPermille = 30;
     P.OutcropJitter = 18;
     P.GroundConfig = SAND_TERRAIN;
     break;

   case TUNDRA:
     P.TreeCoverage = 0.006;
     P.CoverageJitter = 0.003;
     P.Cells[0] = 16; P.Cells[1] = 8; P.Cells[2] = 4;
     P.PrimarySpecies = DWARF_BIRCH;
     P.SecondarySpecies = DWARF_BIRCH;
     P.OutcropPermille = 40;
     P.OutcropJitter = 25;
     P.GroundConfig = SNOW_TERRAIN;
     P.RichGroundConfig = GRASS_TERRAIN;
     P.RichGroundPercent = 25;
     P.Boulder = SNOW_BOULDER;
     break;

   case GLACIER:
     P.Cells[0] = 20; P.Cells[1] = 10; P.Cells[2] = 5;
     P.GroundConfig = SNOW_TERRAIN;
     P.RichGroundConfig = GLACIER_ICE;
     P.RichGroundPercent = 34;
     P.WallPermille = 220;
     P.FractureArea = 1500;
     P.OutcropPermille = 25;
     P.OutcropJitter = 15;
     P.Boulder = SNOW_BOULDER;
     break;

   case OCEAN_LEVEL:
     break;
  }

  return P;
}

/* Occasional coherent wet features. The passed field is canopy density, not a
   height map, so this is a deliberate heuristic: sampling several candidates
   and keeping the lowest-valued one places water toward the open clearings the
   canopy leaves. It does not prove a geographic depression, and a real
   height/moisture model would be the way to get that. Not every map need show
   open water; a damp climate can be carried by the ground palette alone. */
void WildernessPlaceWetFeatures(const wildernessbiome& P,
				const wildernessterrainnoise& Field,
				int XSize, int YSize, lsquare*** Map)
{
  if(P.WaterArea <= 0 || P.WaterChance <= 0)
    return;

  int Features = Max(1, XSize * YSize / P.WaterArea);

  for(int c = 0; c < Features; ++c)
  {
    if(int(RAND() % 100) >= P.WaterChance)
      continue;

    v2 Center(0, 0);
    double Best = 2.0;

    for(int Trial = 0; Trial < 4; ++Trial)
    {
      v2 Candidate(4 + RAND_N(Max(1, XSize - 8)),
		   4 + RAND_N(Max(1, YSize - 8)));
      double Value = Field.At(Candidate.X, Candidate.Y);

      if(Value < Best)
      {
	Best = Value;
	Center = Candidate;
      }
    }

    WildernessPlaceWater(Map, Center, 1 + RAND_N(P.WaterMaxRadius), POOL,
			 XSize, YSize);
  }
}

void level::GenerateJungle()
{
  int x, y;
  const wildernessbiome P = WildernessBiomeForType(JUNGLE);
  int Cells[3];
  WildernessJitterCells(P.Cells, Cells);

  /* A broad, correlated canopy thresholded at a per-map percentile, so the
     target coverage is met while stands stay large, uneven and ragged-edged.
     Part of the vegetation becomes opaque canopy and a little becomes
     impassable thicket, so a jungle stand conceals and resists as well as
     looks dense. */
  wildernessterrainnoise Canopy, Species;
  Canopy.Init(XSize, YSize, Cells);
  Species.Init(XSize, YSize, Cells);
  double Coverage = P.TreeCoverage + WildernessJitter(P.CoverageJitter);
  double Threshold = WildernessCoverageThreshold(Canopy, XSize, YSize, Coverage);
  double RichThreshold = WildernessCoverageThreshold(
    Canopy, XSize, YSize, Coverage + P.RichGroundPercent / 100.0);

  for(x = 0; x < XSize; ++x)
    for(y = 0; y < YSize; ++y)
      /* Damp ground follows the canopy instead of per-cell speckling, so
	 thickets read as thickets. */
      Map[x][y]->SetLTerrain(solidterrain::Spawn(
	Canopy.At(x, y) >= RichThreshold ? P.RichGroundConfig : P.GroundConfig), 0);

  /* Wet depressions, biased toward low ground rather than demanded on every
     tile. */
  WildernessPlaceWetFeatures(P, Canopy, XSize, YSize, Map);

  for(x = 0; x < XSize; ++x)
    for(y = 0; y < YSize; ++y)
    {
      if(Canopy.At(x, y) < Threshold
	 || !(Map[x][y]->GetWalkability() & WALK))
	continue;

      if(!(RAND() % P.RockChance))
      {
	Map[x][y]->ChangeOLTerrain(boulder::Spawn(1 + RAND_2));
	continue;
      }

      int Roll = int(RAND() % 100);
      int Config;

      if(Roll < P.ThicketPercent)
	Config = P.ThicketSpecies;
      else if(Roll < P.ThicketPercent + P.CanopyPercent)
	Config = P.CanopySpecies;
      else
	Config = Species.At(x, y) >= 0.5 ? P.PrimarySpecies : P.SecondarySpecies;

      Map[x][y]->ChangeOLTerrain(decoration::Spawn(Config));
    }
}

void level::CreateTunnelNetwork(int MinLength, int MaxLength, int MinNodes, int MaxNodes, v2 StartPos)
{
  v2 Pos = StartPos, Direction;
  int Length;
  game::BusyAnimation();
  FlagMap[Pos.X][Pos.Y] = PREFERRED;

  for(int c1 = 0; c1 < MaxNodes; ++c1)
  {
    Direction = game::GetBasicMoveVector(RAND() % 4);
    Length = MinLength + RAND_N(MaxLength - MinLength + 1);

    for(int c2 = 0; c2 < Length; ++c2)
    {
      if(IsValidPos(Direction + Pos))
      {
	Pos += Direction;
	FlagMap[Pos.X][Pos.Y] = PREFERRED;
      }
      else
      {
	if(c1 >= MinNodes)
	  return;

	break;
      }
    }
  }
}

void level::GenerateSteppe()
{
  int x, y;
  const wildernessbiome P = WildernessBiomeForType(STEPPE);
  int Cells[3];
  WildernessJitterCells(P.Cells, Cells);

  /* Open ground with broad dry/grassy variation, irregular rock outcrops whose
     density varies from map to map, and only a few small sheltered groves. It
     stays a steppe, not a savanna. */
  wildernessterrainnoise Ground, Rock, Grove;
  Ground.Init(XSize, YSize, Cells);
  Rock.Init(XSize, YSize, Cells);
  Grove.Init(XSize, YSize, Cells);
  double DarkThreshold = WildernessCoverageThreshold(
    Ground, XSize, YSize, P.RichGroundPercent / 100.0);
  double RockCoverage = Max(0.001,
    (P.OutcropPermille + WildernessJitter(P.OutcropJitter)) / 1000.0);
  double RockThreshold = WildernessCoverageThreshold(
    Rock, XSize, YSize, RockCoverage);
  double GroveCoverage = Max(0.0002,
    P.TreeCoverage + WildernessJitter(P.CoverageJitter));
  double GroveThreshold = WildernessCoverageThreshold(
    Grove, XSize, YSize, GroveCoverage);

  for(x = 0; x < XSize; ++x)
    for(y = 0; y < YSize; ++y)
      Map[x][y]->SetLTerrain(solidterrain::Spawn(
	Ground.At(x, y) >= DarkThreshold ? P.RichGroundConfig : P.GroundConfig), 0);

  for(x = 0; x < XSize; ++x)
    for(y = 0; y < YSize; ++y)
    {
      if(Rock.At(x, y) >= RockThreshold)
	Map[x][y]->ChangeOLTerrain(boulder::Spawn(1 + RAND_2));
      else if(Grove.At(x, y) >= GroveThreshold
	      && Ground.At(x, y) >= DarkThreshold)
	/* Groves sit in the richer, damper ground rather than out in the open,
	   so they read as sheltered spots. */
	Map[x][y]->ChangeOLTerrain(decoration::Spawn(RAND_2 ? OAK : BIRCH));
    }
}

void level::GenerateDesert()
{
  int x, y;
  const wildernessbiome P = WildernessBiomeForType(DESERT);
  int Cells[3];
  WildernessJitterCells(P.Cells, Cells);

  for(x = 0; x < XSize; ++x)
    for(y = 0; y < YSize; ++y)
      Map[x][y]->SetLTerrain(solidterrain::Spawn(P.GroundConfig), 0);

  /* Rocks grouped into outcrops rather than scattered independently, with the
     outcrop density varying between maps. */
  wildernessterrainnoise Rock;
  Rock.Init(XSize, YSize, Cells);
  double RockCoverage = Max(0.001,
    (P.OutcropPermille + WildernessJitter(P.OutcropJitter)) / 1000.0);
  double RockThreshold = WildernessCoverageThreshold(
    Rock, XSize, YSize, RockCoverage);

  for(x = 0; x < XSize; ++x)
    for(y = 0; y < YSize; ++y)
      if(Rock.At(x, y) >= RockThreshold)
	Map[x][y]->ChangeOLTerrain(boulder::Spawn(1 + RAND_2));

  /* Sparse cactus, still IVAN's stylized shorthand, but in small clusters
     whose count scales with the map area. */
  int Clusters = Max(2, XSize * YSize / 1500) + RAND_N(3);

  for(int c = 0; c < Clusters; ++c)
  {
    v2 Center(RAND_N(XSize), RAND_N(YSize));
    int Count = 1 + RAND_N(3);

    for(int k = 0; k < Count; ++k)
    {
      v2 Pos = Center + v2(RAND_N(3) - 1, RAND_N(3) - 1);

      if(IsValidPos(Pos))
	Map[Pos.X][Pos.Y]->ChangeOLTerrain(decoration::Spawn(CACTUS));
    }
  }

  /* A rare oasis: open water, a grassy ground transition around it, and only
     then a thinning ring of palms, rather than a bare disc with a hard edge. */
  if(!(RAND() % 3))
  {
    v2 Center(5 + RAND_N(Max(1, XSize - 10)), 5 + RAND_N(Max(1, YSize - 10)));
    int Radius = 1 + RAND_N(2);
    WildernessPlaceWater(Map, Center, Radius, POOL, XSize, YSize);

    int GroundRadius = Radius + 2;

    for(int dx = -GroundRadius; dx <= GroundRadius; ++dx)
      for(int dy = -GroundRadius; dy <= GroundRadius; ++dy)
      {
	v2 Pos = Center + v2(dx, dy);

	if(!IsValidPos(Pos))
	  continue;

	int Dist = (Pos - Center).GetLengthSquare();

	if(Dist <= Radius * Radius || Dist > GroundRadius * GroundRadius)
	  continue;

	if(!(Map[Pos.X][Pos.Y]->GetWalkability() & WALK))
	  continue;

	Map[Pos.X][Pos.Y]->ChangeOLTerrain(0);
	Map[Pos.X][Pos.Y]->ChangeGLTerrain(solidterrain::Spawn(GRASS_TERRAIN));

	if(Dist <= (Radius + 1) * (Radius + 1) && !(RAND() % 2))
	  Map[Pos.X][Pos.Y]->ChangeOLTerrain(decoration::Spawn(PALM));
      }
  }
}

void level::GenerateLeafyForest()
{
  int x, y;
  const wildernessbiome P = WildernessBiomeForType(LEAFY_FOREST);
  int Cells[3];
  WildernessJitterCells(P.Cells, Cells);

  /* Wider, uneven oak/birch stands with real clearings, a litter floor under
     the trees instead of uniform lawn, and occasional damp hollows. Teak is
     dropped: it is a tropical species, not part of a temperate palette. */
  wildernessterrainnoise Canopy, Species;
  Canopy.Init(XSize, YSize, Cells);
  Species.Init(XSize, YSize, Cells);
  double Coverage = P.TreeCoverage + WildernessJitter(P.CoverageJitter);
  double Threshold = WildernessCoverageThreshold(Canopy, XSize, YSize, Coverage);
  double RichThreshold = WildernessCoverageThreshold(
    Canopy, XSize, YSize, Coverage + P.RichGroundPercent / 100.0);

  for(x = 0; x < XSize; ++x)
    for(y = 0; y < YSize; ++y)
      Map[x][y]->SetLTerrain(solidterrain::Spawn(
	Canopy.At(x, y) >= RichThreshold ? P.RichGroundConfig : P.GroundConfig), 0);

  WildernessPlaceWetFeatures(P, Canopy, XSize, YSize, Map);

  for(x = 0; x < XSize; ++x)
    for(y = 0; y < YSize; ++y)
    {
      if(Canopy.At(x, y) < Threshold
	 || !(Map[x][y]->GetWalkability() & WALK))
	continue;

      if(!(RAND() % P.RockChance))
	Map[x][y]->ChangeOLTerrain(boulder::Spawn(1 + RAND_2));
      else
	Map[x][y]->ChangeOLTerrain(decoration::Spawn(
	  Species.At(x, y) >= 0.5 ? P.PrimarySpecies : P.SecondarySpecies));
    }
}

void level::GenerateEvergreenForest()
{
  int x, y;
  const wildernessbiome P = WildernessBiomeForType(EVERGREEN_FOREST);
  int Cells[3];
  WildernessJitterCells(P.Cells, Cells);
  const int SnowCells[3] = { 20, 10, 5 };

  /* Denser pine/fir stands with fewer, narrower clearings than the leafy
     forest. Snow collects in the exposed clearings rather than under the
     canopy, so it reads as a patchy winter forest instead of per-cell white
     noise on a separate field. */
  wildernessterrainnoise Canopy, Species, Snow;
  Canopy.Init(XSize, YSize, Cells);
  Species.Init(XSize, YSize, Cells);
  Snow.Init(XSize, YSize, SnowCells);
  double Coverage = P.TreeCoverage + WildernessJitter(P.CoverageJitter);
  double Threshold = WildernessCoverageThreshold(Canopy, XSize, YSize, Coverage);
  double RichThreshold = WildernessCoverageThreshold(
    Canopy, XSize, YSize, Coverage + P.RichGroundPercent / 100.0);
  double SnowThreshold = WildernessCoverageThreshold(
    Snow, XSize, YSize, P.SnowPercent / 100.0);

  for(x = 0; x < XSize; ++x)
    for(y = 0; y < YSize; ++y)
    {
      int Config = P.GroundConfig;

      if(Canopy.At(x, y) >= RichThreshold)
	Config = P.RichGroundConfig;
      else if(Snow.At(x, y) >= SnowThreshold)
	Config = P.SnowConfig;

      Map[x][y]->SetLTerrain(solidterrain::Spawn(Config), 0);
    }

  WildernessPlaceWetFeatures(P, Canopy, XSize, YSize, Map);

  for(x = 0; x < XSize; ++x)
    for(y = 0; y < YSize; ++y)
    {
      if(Canopy.At(x, y) < Threshold
	 || !(Map[x][y]->GetWalkability() & WALK))
	continue;

      if(!(RAND() % P.RockChance))
	Map[x][y]->ChangeOLTerrain(boulder::Spawn(1 + RAND_2));
      else
	Map[x][y]->ChangeOLTerrain(decoration::Spawn(
	  Species.At(x, y) >= 0.5 ? P.PrimarySpecies : P.SecondarySpecies));
    }
}

void level::GenerateTundra()
{
  int x, y;
  const wildernessbiome P = WildernessBiomeForType(TUNDRA);
  int Cells[3];
  WildernessJitterCells(P.Cells, Cells);

  /* Connected exposed ground amid the snow, with the exposed fraction carried
     by the profile, plus rock outcrops and sparse dwarf birch. */
  wildernessterrainnoise Exposed, Outcrop, Grove;
  Exposed.Init(XSize, YSize, Cells);
  Outcrop.Init(XSize, YSize, Cells);
  Grove.Init(XSize, YSize, Cells);
  double ExposedThreshold = WildernessCoverageThreshold(
    Exposed, XSize, YSize, P.RichGroundPercent / 100.0);
  double RockCoverage = Max(0.001,
    (P.OutcropPermille + WildernessJitter(P.OutcropJitter)) / 1000.0);
  double RockThreshold = WildernessCoverageThreshold(
    Outcrop, XSize, YSize, RockCoverage);
  double GroveCoverage = Max(0.0002,
    P.TreeCoverage + WildernessJitter(P.CoverageJitter));
  double GroveThreshold = WildernessCoverageThreshold(
    Grove, XSize, YSize, GroveCoverage);

  for(x = 0; x < XSize; ++x)
    for(y = 0; y < YSize; ++y)
      Map[x][y]->SetLTerrain(solidterrain::Spawn(
	Exposed.At(x, y) >= ExposedThreshold ? P.RichGroundConfig : P.GroundConfig), 0);

  for(x = 0; x < XSize; ++x)
    for(y = 0; y < YSize; ++y)
    {
      if(Outcrop.At(x, y) >= RockThreshold)
	Map[x][y]->ChangeOLTerrain(boulder::Spawn(P.Boulder));
      else if(Grove.At(x, y) >= GroveThreshold)
	Map[x][y]->ChangeOLTerrain(decoration::Spawn(P.PrimarySpecies));
    }
}

void level::GenerateGlacier()
{
  int x, y;
  const wildernessbiome P = WildernessBiomeForType(GLACIER);
  int Cells[3];
  WildernessJitterCells(P.Cells, Cells);

  /* A glacier is a continuous ice surface broken by raised features, not
     snow-covered ground studded with wall islands. Exposed ice ground replaces
     snow over broad correlated regions; the walls are correlated ice masses;
     stone outcrops follow their own field instead of a fixed screen-border
     band; and a few short straight wall segments suggest a fracture pattern.
     Those segments are raised obstacles, not drops into a physical crevasse:
     they block walking and can be destroyed, which is deliberately safe
     stylized texture rather than a fall hazard.
     PrepareWildernessEntry() then validates reachability and carves whatever
     opening is actually needed, so this can never seal the player in. */
  wildernessterrainnoise Ice, Mass, Outcrop;
  Ice.Init(XSize, YSize, Cells);
  Mass.Init(XSize, YSize, Cells);
  Outcrop.Init(XSize, YSize, Cells);
  double IceThreshold = WildernessCoverageThreshold(
    Ice, XSize, YSize, P.RichGroundPercent / 100.0);
  double MassCoverage = Max(0.02,
    (P.WallPermille + WildernessJitter(60)) / 1000.0);
  double MassThreshold = WildernessCoverageThreshold(
    Mass, XSize, YSize, MassCoverage);
  double OutcropCoverage = Max(0.001,
    (P.OutcropPermille + WildernessJitter(P.OutcropJitter)) / 1000.0);
  double OutcropThreshold = WildernessCoverageThreshold(
    Outcrop, XSize, YSize, OutcropCoverage);

  for(x = 0; x < XSize; ++x)
    for(y = 0; y < YSize; ++y)
      Map[x][y]->SetLTerrain(solidterrain::Spawn(
	Ice.At(x, y) >= IceThreshold ? P.RichGroundConfig : P.GroundConfig), 0);

  for(x = 0; x < XSize; ++x)
    for(y = 0; y < YSize; ++y)
    {
      if(Mass.At(x, y) >= MassThreshold)
	Map[x][y]->ChangeOLTerrain(wall::Spawn(ICE_WALL));
      else if(Outcrop.At(x, y) >= OutcropThreshold)
	Map[x][y]->ChangeOLTerrain(wall::Spawn(STONE_WALL));
      else if(!(RAND() % 60))
	Map[x][y]->ChangeOLTerrain(boulder::Spawn(P.Boulder));
    }

  /* Fracture lines: short straight wall segments that read as cracks in the
     surface rather than the round blobs a thresholded field makes. They are
     raised and destroyable, not holes, so they never need a fall mechanic. */
  int Fractures = Max(2, XSize * YSize / P.FractureArea);

  for(int c = 0; c < Fractures; ++c)
  {
    v2 Pos(RAND_N(XSize), RAND_N(YSize));
    truth Vertical = RAND_2;
    int Length = 3 + RAND_N(9);

    for(int s = 0; s < Length; ++s)
    {
      v2 Q = Pos + (Vertical ? v2(0, s) : v2(s, 0));

      if(IsValidPos(Q) && (Map[Q.X][Q.Y]->GetWalkability() & WALK))
	Map[Q.X][Q.Y]->ChangeOLTerrain(wall::Spawn(ICE_WALL));
    }
  }
}

void level::GenerateOcean()
{
  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
      Map[x][y]->SetLTerrain(liquidterrain::Spawn(POOL), 0);
}

/* Builds a complete outdoor wilderness level for one world-map tile. The
   biome-specific terrain builders only fill the already allocated squares;
   this pipeline adds the shared runtime initialization, a guaranteed entry
   point with routes to the map edges, and the one-time biome population. */

void level::GenerateWilderness()
{
  const festring* Msg = LevelScript->GetLevelMessage();

  if(Msg)
    LevelMessage = *Msg;

  /* A tile's encoded coordinate is not dungeon depth: all deltas are zero.
     Initialize every statistic anyway so save/load never reads garbage when
     generic monster generation is disabled. */
  InitializeRuntimeStats();
  NightAmbientLuminance = MakeRGB24(70, 70, 70);

  switch(*LevelScript->GetType())
  {
   case DESERT: GenerateDesert(); break;
   case JUNGLE: GenerateJungle(); break;
   case STEPPE: GenerateSteppe(); break;
   case LEAFY_FOREST: GenerateLeafyForest(); break;
   case EVERGREEN_FOREST: GenerateEvergreenForest(); break;
   case TUNDRA: GenerateTundra(); break;
   case GLACIER: GenerateGlacier(); break;
   case OCEAN_LEVEL: GenerateOcean(); break;
   default: ABORT("Unknown wilderness biome type %d!", *LevelScript->GetType());
  }

  PrepareWildernessEntry();
  PopulateWilderness(v2(XSize / 2, YSize / 2));

  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
    {
      Map[x][y]->CalculateGroundBorderPartners();
      Map[x][y]->CalculateOverBorderPartners();
    }
}

/* Script-derived defaults for a level that is being created for the first
   time. */
void level::InitializeRuntimeStats()
{
  int Index = GetIndex();

  Difficulty = 0.001 * (*LevelScript->GetDifficultyBase()
			+ *LevelScript->GetDifficultyDelta() * Index);
  MonsterGenerationInterval = *LevelScript->GetMonsterGenerationIntervalBase()
    + *LevelScript->GetMonsterGenerationIntervalDelta() * Index;
  IdealPopulation = *LevelScript->GetMonsterAmountBase()
    + *LevelScript->GetMonsterAmountDelta() * Index;
  RestoreNonSerializedStats();
}

/* Difficulty, MonsterGenerationInterval and IdealPopulation are part of the
   serialized level and must survive a reload unchanged, so only the script
   derived statistics that level::Save() does not write are restored here.
   Shared by wilderness generation and level loading so the two paths cannot
   drift. */
void level::RestoreNonSerializedStats()
{
  int Index = GetIndex();

  EnchantmentMinusChance = *LevelScript->GetEnchantmentMinusChanceBase()
    + *LevelScript->GetEnchantmentMinusChanceDelta() * Index;
  EnchantmentPlusChance = *LevelScript->GetEnchantmentPlusChanceBase()
    + *LevelScript->GetEnchantmentPlusChanceDelta() * Index;

  /* Whether this level owns a weather cycle is a property of its biome, so it
     is recomputed after every load rather than serialized. */
  WeatherEnabled = IsWeatherBiome(LevelScript->GetType() ? *LevelScript->GetType() : 0);
}

int level::GetWildernessGroundConfig() const
{
  switch(*LevelScript->GetType())
  {
   case DESERT: return SAND_TERRAIN;
   case TUNDRA:
   case GLACIER: return SNOW_TERRAIN;
   default: return GRASS_TERRAIN;
  }
}

void level::PrepareWildernessEntry()
{
  int CenterX = XSize / 2;
  int CenterY = YSize / 2;
  truth Ocean = *LevelScript->GetType() == OCEAN_LEVEL;
  int GroundConfig = GetWildernessGroundConfig();

  /* The arrival clearing is a small irregular dell nudged off the exact map
     centre, so every local area does not open on the same carved square stamp.
     The offset is bounded and the radius is large enough that the exact centre
     stays clear for anything that still lands there, and the world-map return
     tile is not affected. */
  int Jitter = 2;
  v2 Entry(CenterX + (RAND_N(2 * Jitter + 1) - Jitter),
	   CenterY + (RAND_N(2 * Jitter + 1) - Jitter));
  int Radius = 3 + RAND_N(2);

  for(int dx = -Radius; dx <= Radius; ++dx)
    for(int dy = -Radius; dy <= Radius; ++dy)
    {
      v2 Pos = Entry + v2(dx, dy);

      if(!IsValidPos(Pos))
	continue;

      if((Pos - Entry).GetLengthSquare() <= Radius * Radius + RAND_N(2))
	MakeWildernessPassable(Pos.X, Pos.Y, GroundConfig, Ocean);
    }

  SetEntryPos(WILDERNESS_LOCAL_ENTRY, Entry);

  if(Ocean)
    return; /* open water already reaches every edge */

  /* Guarantee a route wide enough for a four-square follower to every edge. */
  ForceWildernessExitRoutes(Entry);
}

void level::ForceWildernessExitRoutes(v2 From)
{
  int GroundConfig = GetWildernessGroundConfig();
  v2 Edges[4] = { v2(0, From.Y), v2(XSize - 1, From.Y),
		  v2(From.X, 0), v2(From.X, YSize - 1) };

  for(int c = 0; c < 4; ++c)
  {
    /* An already open biome carries a wide route on its own; repainting its
       usable ground would only stamp the same road cross on every region.
       Carve a meandering opening only where the footprint-aware check
       actually fails, so the old narrow-route bug cannot return. */
    if(!WildernessWideRouteExists(From, Edges[c]))
      CarveWildernessTrail(From, Edges[c], GroundConfig);

    if(!WildernessWideRouteExists(From, Edges[c]))
      ABORT("Wilderness exit corridor %d could not be opened!", c);
  }
}

/* Movement-appropriate reachability for a four-square creature: a parent
   square counts only when it and its right, lower and lower-right neighbours
   are all walkable, and success is reaching the square next to the target
   edge. */
truth level::WildernessWideRouteExists(v2 From, v2 To) const
{
  if(!IsValidPos(From) || !IsValidPos(To))
    return false;

  std::vector<char> Wide(XSizeTimesYSize, 0);

  for(int x = 0; x + 1 < XSize; ++x)
    for(int y = 0; y + 1 < YSize; ++y)
      Wide[y * XSize + x] = (Map[x][y]->GetWalkability() & WALK)
	&& (Map[x + 1][y]->GetWalkability() & WALK)
	&& (Map[x][y + 1]->GetWalkability() & WALK)
	&& (Map[x + 1][y + 1]->GetWalkability() & WALK);

  v2 Start = ERROR_V2;

  for(int dx = -1; dx <= 1 && Start == ERROR_V2; ++dx)
    for(int dy = -1; dy <= 1 && Start == ERROR_V2; ++dy)
    {
      v2 Pos = From + v2(dx, dy);

      if(IsValidPos(Pos) && Wide[Pos.Y * XSize + Pos.X])
	Start = Pos;
    }

  if(Start == ERROR_V2)
    return false;

  std::vector<char> Seen(XSizeTimesYSize, 0);
  std::vector<v2> Stack;
  Seen[Start.Y * XSize + Start.X] = 1;
  Stack.push_back(Start);

  while(!Stack.empty())
  {
    v2 Pos = Stack.back();
    Stack.pop_back();

    if((To.X == 0 && Pos.X == 0)
       || (To.X == XSize - 1 && Pos.X == XSize - 2)
       || (To.Y == 0 && Pos.Y == 0)
       || (To.Y == YSize - 1 && Pos.Y == YSize - 2))
      return true;

    /* Characters move in eight directions, so two parent squares that touch
       only at a corner still connect for a four-square creature. */
    for(int d = 0; d < 8; ++d)
    {
      v2 Next = Pos + game::GetMoveVector(d);

      if(!IsValidPos(Next))
	continue;

      int I = Next.Y * XSize + Next.X;

      if(Seen[I] || !Wide[I])
	continue;

      Seen[I] = 1;
      Stack.push_back(Next);
    }
  }

  return false;
}

truth level::WildernessRouteExists(v2 From, v2 To) const
{
  if(!IsValidPos(From) || !IsValidPos(To))
    return false;

  if(!(Map[From.X][From.Y]->GetWalkability() & WALK))
    return false;

  std::vector<char> Seen(XSizeTimesYSize, 0);
  std::vector<v2> Stack;
  Seen[From.Y * XSize + From.X] = 1;
  Stack.push_back(From);

  while(!Stack.empty())
  {
    v2 Pos = Stack.back();
    Stack.pop_back();

    if(Pos == To)
      return true;

    for(int d = 0; d < 8; ++d)
    {
      v2 Next = Pos + game::GetMoveVector(d);

      if(!IsValidPos(Next))
	continue;

      int Index = Next.Y * XSize + Next.X;

      if(Seen[Index] || !(Map[Next.X][Next.Y]->GetWalkability() & WALK))
	continue;

      Seen[Index] = 1;
      Stack.push_back(Next);
    }
  }

  return false;
}

void level::CarveWildernessTrail(v2 From, v2 To, int GroundConfig)
{
  /* A gently meandering band rather than a ruler-straight line, so the four
     routes do not stamp the same cross on every biome. The lateral drift moves
     at most one square per step and is bounded, which keeps the two-square
     clearance a large follower needs. */
  truth Horizontal = From.Y == To.Y;
  v2 Pos = From;
  int Drift = 0;

  while(Pos != To)
  {
    if(Horizontal)
      Pos.X += To.X > From.X ? 1 : -1;
    else
      Pos.Y += To.Y > From.Y ? 1 : -1;

    if(!(RAND() % 3))
      Drift += RAND_2 ? 1 : -1;

    if(Drift > 2)
      Drift = 2;

    if(Drift < -2)
      Drift = -2;

    for(int d = -1; d <= 1; ++d)
    {
      v2 P = Pos + (Horizontal ? v2(0, Drift + d) : v2(Drift + d, 0));

      if(IsValidPos(P))
	MakeWildernessPassable(P.X, P.Y, GroundConfig, false);
    }
  }
}

void level::MakeWildernessPassable(int X, int Y, int GroundConfig, truth Ocean)
{
  lsquare* Square = Map[X][Y];

  if(Square->GetOLTerrain())
    Square->ChangeOLTerrain(0);

  if(Ocean)
    Square->ChangeGLTerrain(liquidterrain::Spawn(POOL));
  else
    Square->ChangeGLTerrain(solidterrain::Spawn(GroundConfig));
}

/* Movement-appropriate reachability: flood-fills from From using only squares
   whose walkability matches MoveType, and reports whether To is reachable. */
truth level::WildernessSquareReachable(v2 From, v2 To, int MoveType) const
{
  if(!IsValidPos(From) || !IsValidPos(To))
    return false;

  if(!(Map[From.X][From.Y]->GetWalkability() & MoveType))
    return false;

  std::vector<char> Seen(XSizeTimesYSize, 0);
  std::vector<v2> Stack;
  Seen[From.Y * XSize + From.X] = 1;
  Stack.push_back(From);

  while(!Stack.empty())
  {
    v2 Pos = Stack.back();
    Stack.pop_back();

    if(Pos == To)
      return true;

    for(int d = 0; d < 8; ++d)
    {
      v2 Next = Pos + game::GetMoveVector(d);

      if(!IsValidPos(Next))
	continue;

      int Index = Next.Y * XSize + Next.X;

      if(Seen[Index] || !(Map[Next.X][Next.Y]->GetWalkability() & MoveType))
	continue;

      Seen[Index] = 1;
      Stack.push_back(Next);
    }
  }

  return false;
}

/* Strict, bounded wilderness placement. Unlike GetRandomSquare() this never
   drops the creature's movement, footprint or occupancy requirements, and it
   rechecks CanMoveOn() and IsFreeForMe() on every candidate. */
v2 level::FindWildernessSpawnSquare(const character* Char, v2 Center, int MinDistance) const
{
  for(int c = 0; c < 200; ++c)
  {
    v2 Candidate(1 + RAND() % (XSize - 2), 1 + RAND() % (YSize - 2));
    lsquare* Square = Map[Candidate.X][Candidate.Y];

    if(!Char->CanMoveOn(Square) || !Char->IsFreeForMe(Square))
      continue;

    if((Candidate - Center).GetManhattanLength() <= MinDistance)
      continue;

    return Candidate;
  }

  return ERROR_V2;
}

/* Strict destination allocator for travel. Unlike GetRandomSquare() this only
   ever returns a square the character can actually use: it must be reachable
   from StartPos through squares the character itself can traverse, and the
   square itself (including the whole footprint of a multi-square creature,
   which IsFreeForMe() covers) must be free. Squares that are already occupied
   are still used for routing, so a companion stuck behind its leader in a
   one-square corridor can still find room; it simply cannot stand on one.
   Returning ERROR_V2 means "no such square exists" and the caller must refuse
   the transfer rather than force a placement. */
v2 level::FindTravelDestination(const character* Char, v2 StartPos) const
{
  if(!Char || !IsValidPos(StartPos))
    return ERROR_V2;

  std::vector<v2> Queue;
  std::vector<char> Seen(XSizeTimesYSize, 0);
  ulong Head = 0;
  Seen[StartPos.Y * XSize + StartPos.X] = 1;
  Queue.push_back(StartPos);

  while(Head < Queue.size())
  {
    v2 Pos = Queue[Head++];

    for(int d = 0; d < 8; ++d)
    {
      v2 Next = Pos + game::GetMoveVector(d);

      if(!IsValidPos(Next))
	continue;

      int Index = Next.Y * XSize + Next.X;

      if(Seen[Index])
	continue;

      Seen[Index] = 1;

      lsquare* Square = GetLSquare(Next);

      if(!Char->CanMoveOn(Square))
	continue;

      if(Char->IsFreeForMe(Square))
	return Next;

      Queue.push_back(Next);
    }
  }

  return ERROR_V2;
}

/* One-time biome population. Concrete configurations are resolved by class id
   and, when needed, by adjective so that abstract bases are never spawned. */

struct wildernessspawn
{
  const char* ClassID;
  const char* Adjective;
  int Chance;
  int Min, Max;
};

static const wildernessspawn WildernessJungleSpawns[] =
{
  { "snake", 0, 100, 1, 3 },
  { "spider", "large", 100, 1, 2 },
  { "carnivorousplant", 0, 70, 1, 2 },
  { "largerat", 0, 50, 1, 1 },
  { "spider", "giant", 15, 1, 1 }
};

static const wildernessspawn WildernessLeafyForestSpawns[] =
{
  { "hedgehog", 0, 100, 1, 2 },
  { "skunk", 0, 80, 1, 1 },
  { "largerat", 0, 80, 1, 2 },
  { "wolf", 0, 60, 1, 1 },
  { "bear", "black", 20, 1, 1 },
  { "magpie", 0, 20, 1, 1 }
};

static const wildernessspawn WildernessEvergreenForestSpawns[] =
{
  { "wolf", 0, 100, 1, 2 },
  { "hedgehog", 0, 80, 1, 2 },
  { "bear", "black", 40, 1, 1 },
  { "bear", "grizzly", 15, 1, 1 },
  { "twoheadedmoose", 0, 10, 1, 1 },
  { "magpie", 0, 20, 1, 1 }
};

static const wildernessspawn WildernessSteppeSpawns[] =
{
  { "jackal", 0, 100, 1, 2 },
  { "snake", 0, 50, 1, 1 },
  { "wolf", 0, 40, 1, 1 },
  { "buffalo", 0, 20, 1, 1 },
  { "lion", 0, 10, 1, 1 }
};

/* Desert lions are omitted: they require oasis/vegetated habitat, which the
   current desert generator does not track for the spawner. */
static const wildernessspawn WildernessDesertSpawns[] =
{
  { "jackal", 0, 100, 1, 2 },
  { "snake", 0, 60, 1, 1 },
  { "spider", "large", 50, 1, 1 }
};

static const wildernessspawn WildernessTundraSpawns[] =
{
  { "wolf", 0, 80, 0, 2 },
  { "mammoth", 0, 15, 1, 1 },
  { "twoheadedmoose", 0, 10, 1, 1 },
  { "bear", "polar", 5, 1, 1 }
};

static const wildernessspawn WildernessGlacierSpawns[] =
{
  { "bear", "polar", 10, 1, 1 }
};

static const wildernessspawn WildernessOceanSpawns[] =
{
  { "dolphin", 0, 80, 0, 3 }
};

void level::PopulateWilderness(v2 Center)
{
  const wildernessspawn* Table = 0;
  int Size = 0;

  switch(*LevelScript->GetType())
  {
   case JUNGLE:
    Table = WildernessJungleSpawns;
    Size = sizeof(WildernessJungleSpawns) / sizeof(wildernessspawn);
    break;
   case LEAFY_FOREST:
    Table = WildernessLeafyForestSpawns;
    Size = sizeof(WildernessLeafyForestSpawns) / sizeof(wildernessspawn);
    break;
   case EVERGREEN_FOREST:
    Table = WildernessEvergreenForestSpawns;
    Size = sizeof(WildernessEvergreenForestSpawns) / sizeof(wildernessspawn);
    break;
   case STEPPE:
    Table = WildernessSteppeSpawns;
    Size = sizeof(WildernessSteppeSpawns) / sizeof(wildernessspawn);
    break;
   case DESERT:
    Table = WildernessDesertSpawns;
    Size = sizeof(WildernessDesertSpawns) / sizeof(wildernessspawn);
    break;
   case TUNDRA:
    Table = WildernessTundraSpawns;
    Size = sizeof(WildernessTundraSpawns) / sizeof(wildernessspawn);
    break;
   case GLACIER:
    Table = WildernessGlacierSpawns;
    Size = sizeof(WildernessGlacierSpawns) / sizeof(wildernessspawn);
    break;
   case OCEAN_LEVEL:
    Table = WildernessOceanSpawns;
    Size = sizeof(WildernessOceanSpawns) / sizeof(wildernessspawn);
    break;
  }

  if(!Table)
    return;

  for(int c = 0; c < Size; ++c)
  {
    const wildernessspawn& Spawn = Table[c];

    if(RAND() % 100 >= Spawn.Chance)
      continue;

    int Count = Spawn.Max > Spawn.Min
      ? Spawn.Min + RAND_N(Spawn.Max - Spawn.Min + 1) : Spawn.Min;

    for(int i = 0; i < Count; ++i)
      SpawnWildernessAnimal(Spawn.ClassID, Spawn.Adjective, Center);
  }
}

character* level::SpawnWildernessAnimal(const char* ClassID, const char* Adjective, v2 Center)
{
  int ProtoIndex = protocontainer<character>::SearchCodeName(ClassID);

  if(!ProtoIndex)
    ABORT("Wilderness spawn table: unknown creature class '%s'!", ClassID);

  const characterprototype* Proto = protocontainer<character>::GetProto(ProtoIndex);
  const characterdatabase*const* ConfigData = Proto->GetConfigData();
  int ConfigSize = Proto->GetConfigSize();
  int Config = -1;
  int DefaultConfig = -1;

  /* Concrete configurations are resolved by an explicit stable key: an exact
     adjective when the table names one, otherwise the species' default
     (unadjectived) configuration. Preferring "whatever concrete config comes
     first" would make the result depend on script ordering. Abstract bases are
     never spawned. */
  for(int c = 0; c < ConfigSize; ++c)
  {
    if(ConfigData[c]->IsAbstract)
      continue;

    if(Adjective)
    {
      if(ConfigData[c]->Adjective == Adjective)
      {
	Config = ConfigData[c]->Config;
	break;
      }
    }
    else
    {
      if(!ConfigData[c]->Adjective.GetSize())
      {
	Config = ConfigData[c]->Config;
	break;
      }

      if(DefaultConfig < 0)
	DefaultConfig = ConfigData[c]->Config;
    }
  }

  if(Config < 0)
    Config = DefaultConfig;

  if(Config < 0)
    ABORT("Wilderness spawn table: no concrete configuration for %s%s%s%s!",
	  ClassID, Adjective ? " [" : "", Adjective ? Adjective : "",
	  Adjective ? "]" : "");

  character* Char = Proto->Spawn(Config);

  if(!Char)
    return 0;

  Char->CalculateEnchantments();

  v2 Pos = ERROR_V2;
  int MoveType = Char->GetMoveType();

  for(int c = 0; c < 10; ++c)
  {
    v2 Candidate = FindWildernessSpawnSquare(Char, Center, 6);

    if(Candidate == ERROR_V2)
      break;

    /* Ground wildlife must lie in the entry's reachable component so it can
       actually reach the player and the exits. */
    if(!WildernessSquareReachable(Center, Candidate, MoveType))
      continue;

    lsquare* Square = GetLSquare(Candidate);

    if(!Char->CanMoveOn(Square) || !Char->IsFreeForMe(Square))
      continue;

    Pos = Candidate;
    break;
  }

  if(Pos == ERROR_V2)
  {
    delete Char;
    return 0;
  }

  Char->PutTo(Pos);
  Char->SetTeam(game::GetTeam(MONSTER_TEAM));
  Char->SetGenerationDanger(Difficulty);
  Char->SignalGeneration();
  Char->SignalNaturalGeneration();
  ivantime Time;
  game::GetTime(Time);
  int Modifier = Time.Day - EDIT_ATTRIBUTE_DAY_MIN;

  if(Modifier > 0)
    Char->EditAllAttributes(Modifier >> EDIT_ATTRIBUTE_DAY_SHIFT);

  return Char;
}

bool nodepointerstorer::operator<(const nodepointerstorer& N) const
{
  /* In the non-euclidean geometry of IVAN, certain very curved paths are as long as straight ones.
     However, they are so ugly that it is best to prefer routes with as few diagonal moves as
     possible without lengthening the travel. */

  if(Node->TotalDistanceEstimate != N.Node->TotalDistanceEstimate)
    return Node->TotalDistanceEstimate > N.Node->TotalDistanceEstimate;
  else
    return Node->Diagonals > N.Node->Diagonals;
}

void node::CalculateNextNodes()
{
  static int TryOrder[8] = { 1, 3, 4, 6, 0, 2, 5, 7 };

  for(int d = 0; d < 8; ++d)
  {
    v2 NodePos = Pos + game::GetMoveVector(TryOrder[d]);

    if(NodePos.X >= 0 && NodePos.Y >= 0 && NodePos.X < XSize && NodePos.Y < YSize)
    {
      node* Node = NodeMap[NodePos.X][NodePos.Y];

      if(!Node->Processed && ((!SpecialMover && RequiredWalkability & WalkabilityMap[NodePos.X][NodePos.Y]) || (SpecialMover && SpecialMover->CanTheoreticallyMoveOn(Node->Square)) || NodePos == To))
      {
	Node->Processed = true;
	Node->Distance = Distance + 1;
	Node->Diagonals = Diagonals;

	if(d >= 4)
	  ++Node->Diagonals;

	Node->Last = this;

	/* We use the heuristic max(abs(distance.x), abs(distance.y)) here,
	   which is exact in the current geometry if the path is open */

	long Remaining = To.X - NodePos.X;

	if(Remaining < NodePos.X - To.X)
	  Remaining = NodePos.X - To.X;

	if(Remaining < NodePos.Y - To.Y)
	  Remaining = NodePos.Y - To.Y;

	if(Remaining < To.Y - NodePos.Y)
	  Remaining = To.Y - NodePos.Y;

	Node->Remaining = Remaining;
	Node->TotalDistanceEstimate = Node->Distance + Node->Remaining;
	NodeQueue->push(nodepointerstorer(Node));
      }
    }
  }
}

/* Finds the shortest (but possibly not the shortest-looking) path between From and To
   if such exists. Returns a pointer to the node associated with the last square or zero if
   a route can't be found. Calling FindRoute again may invalidate the node, so you must
   store the path in another format ASAP. */

node* level::FindRoute(v2 From, v2 To, const std::set<v2>& Illegal, int RequiredWalkability, const character* SpecialMover)
{
  node::NodeMap = NodeMap;
  node::RequiredWalkability = RequiredWalkability;
  node::SpecialMover = SpecialMover;
  node::To = To;
  node::WalkabilityMap = WalkabilityMap;
  node::XSize = XSize;
  node::YSize = YSize;

  if(!Illegal.empty() && Illegal.find(To) != Illegal.end())
    return 0;

  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
      NodeMap[x][y]->Processed = false;

  node* Node = NodeMap[From.X][From.Y];
  Node->Last = 0;
  Node->Processed = true;
  Node->Distance = 0;
  Node->Diagonals = 0;
  /* The queue is static so that the address stored in node::NodeQueue does
     not dangle once this function returns.  It must nevertheless start every
     search empty: the loop below returns as soon as To is reached, so a stale
     node left over from the previous search would be processed again without
     being marked Processed, corrupting the ->Last chain into a cycle (which
     CreateRoute then walks forever until memory is exhausted). */
  static nodequeue NodeQueue;
  NodeQueue = nodequeue();
  NodeQueue.push(nodepointerstorer(Node));
  node::NodeQueue = &NodeQueue;

  while(!NodeQueue.empty())
  {
    Node = NodeQueue.top().Node;
    NodeQueue.pop();

    if(Node->Pos == To)
      return Node;

    if(Illegal.empty() || Illegal.find(Node->Pos) == Illegal.end())
      Node->CalculateNextNodes();
  }

  return 0;
}

/* All items on ground are moved to the IVector and all characters to CVector */

void level::CollectEverything(itemvector& IVector, charactervector& CVector)
{
  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
    {
      lsquare* LS = Map[x][y];
      LS->GetStack()->MoveItemsTo(IVector, CENTER);
      character* C = LS->GetCharacter();

      if(C && !C->IsPlayer())
      {
	C->Remove();
	CVector.push_back(C);
      }
    }
}

void level::CreateGlobalRain(liquid* Liquid, v2 Speed)
{
  GlobalRainLiquid = Liquid;
  GlobalRainSpeed = Speed;

  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
      if(!Map[x][y]->IsInside())
	Map[x][y]->AddRain(Liquid, Speed, MONSTER_TEAM, false);
}

void level::CheckSunLight()
{
  if(Index == 0 && GetDungeon()->GetIndex() == NEW_ATTNAM)
  {
    double Cos = cos(FPI * (game::GetTick() % 48000) / 24000.);

    if(Cos > 0.01)
    {
      int E = int(100 + Cos * 30);
      SunLightEmitation = MakeRGB24(E, E, E);
      AmbientLuminance = MakeRGB24(E - 6, E - 6, E - 6);
    }
    else
    {
      SunLightEmitation = 0;
      AmbientLuminance = NightAmbientLuminance;
    }
  }
  else if(Index == 0 && GetDungeon()->GetIndex() == ATTNAM)
  {
    double Cos = cos(FPI * (game::GetTick() % 48000) / 24000.);

    if(Cos > 0.41)
    {
      int E = int(100 + (Cos - 0.40) * 40);
      SunLightEmitation = MakeRGB24(E, E, E);
      AmbientLuminance = MakeRGB24(E - 8, E - 8, E - 8);
    }
    else
    {
      SunLightEmitation = 0;
      AmbientLuminance = NightAmbientLuminance;
    }
  }
  else if(game::IsWildernessDungeon(GetDungeon()->GetIndex()))
  {
    double Cos = cos(FPI * (game::GetTick() % 48000) / 24000.);

    if(Cos > 0.01)
    {
      int E = int(100 + Cos * 30);
      SunLightEmitation = MakeRGB24(E, E, E);
      AmbientLuminance = MakeRGB24(E - 6, E - 6, E - 6);
    }
    else
    {
      SunLightEmitation = 0;
      AmbientLuminance = NightAmbientLuminance;
    }
  }
  else
    return;

  SunLightDirection = game::GetSunLightDirectionVector();
  ChangeSunLight();
}

void level::ChangeSunLight()
{
  truth SunSet = game::IsDark(SunLightEmitation);
  ulong c;

  for(c = 0; c < XSizeTimesYSize; ++c)
    Map[0][c]->RemoveSunLight();

  if(!SunSet)
    EmitSunBeams();

  for(c = 0; c < XSizeTimesYSize; ++c)
  {
    lsquare* Square = Map[0][c];

    if(Square->Flags & IS_TRANSPARENT)
      Square->CalculateSunLightLuminance(EMITTER_SQUARE_PART_BITS);

    if(!Square->IsInside())
      Square->AmbientLuminance = AmbientLuminance;

    Square->SendSunLightSignals();
  }

  for(c = 0; c < XSizeTimesYSize; ++c)
    Map[0][c]->CheckIfIsSecondarySunLightEmitter();
}

void level::InitSquarePartEmitationTicks()
{
  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
      Map[x][y]->SquarePartEmitationTick = 0;
}

truth level::GenerateWindows(int X, int Y) const
{
  olterrain* Terrain = Map[X][Y]->GetOLTerrain();

  if(Terrain && Terrain->CreateWindowConfigurations() && !(RAND() % 6))
  {
    Terrain->SetConfig(Terrain->GetConfig() | WINDOW);
    Map[X][Y]->CalculateIsTransparent();
    return true;
  }

  return false;
}

struct sunbeamcontroller : public stackcontroller
{
  static truth Handler(int, int);
  static void ProcessStack();
  static ulong ID;
  static int SunLightBlockHeight;
  static v2 SunLightBlockPos;
  static truth ReSunEmitation;
};

ulong sunbeamcontroller::ID;
int sunbeamcontroller::SunLightBlockHeight;
v2 sunbeamcontroller::SunLightBlockPos;
truth sunbeamcontroller::ReSunEmitation;

void level::ForceEmitterNoxify(const emittervector& Emitter) const
{
  for(emittervector::const_iterator i = Emitter.begin(); i != Emitter.end(); ++i)
  {
    ulong ID = i->ID;
    lsquare* Square = GetLSquare(ExtractPosFromEmitterID(ID));

    if(ID & SECONDARY_SUN_LIGHT)
      Square->Noxify(Square->SecondarySunLightEmitation, SECONDARY_SUN_LIGHT);
    else
      Square->Noxify(Square->Emitation);
  }
}

void level::ForceEmitterEmitation(const emittervector& Emitter, const sunemittervector& SunEmitter, ulong IDFlags) const
{
  for(emittervector::const_iterator i = Emitter.begin(); i != Emitter.end(); ++i)
  {
    ulong ID = i->ID;
    lsquare* Square = GetLSquare(ExtractPosFromEmitterID(ID));

    if(ID & SECONDARY_SUN_LIGHT)
      Square->Emitate(Square->SecondarySunLightEmitation, SECONDARY_SUN_LIGHT|IDFlags);
    else
      Square->Emitate(Square->Emitation, IDFlags);
  }

  {
    stackcontroller::Map = Map;
    stackcontroller::Stack = SquareStack;
    stackcontroller::StackIndex = 0;
    stackcontroller::LevelXSize = XSize;
    stackcontroller::LevelYSize = YSize;
    sunbeamcontroller::ReSunEmitation = true;

    for(sunemittervector::const_iterator i = SunEmitter.begin(); i != SunEmitter.end(); ++i)
    {
      ulong ID = ((*i & ~(EMITTER_SHADOW_BITS | EMITTER_SQUARE_PART_BITS)) | RE_SUN_EMITATED), SourceFlags;
      int X, Y;

      if(ID & ID_X_COORDINATE)
      {
	X = (ID & EMITTER_IDENTIFIER_BITS) - (XSize << 3);
	Y = ID & ID_BEGIN ? -1 : YSize;
	SourceFlags = ID & ID_BEGIN ? SP_BOTTOM : SP_TOP;
      }
      else
      {
	X = ID & ID_BEGIN ? -1 : XSize;
	Y = (ID & EMITTER_IDENTIFIER_BITS) - (YSize << 3);
	SourceFlags = ID & ID_BEGIN ? SP_RIGHT : SP_LEFT;
      }

      EmitSunBeam(v2(X, Y), ID, SourceFlags);
    }

    sunbeamcontroller::ProcessStack();
  }
}

struct loscontroller : public tickcontroller, public stackcontroller
{
  static truth Handler(int x, int y)
  {
    lsquare* Square = Map[x >> 1][y >> 1];
    const ulong SquareFlags = Square->Flags;

    if(SquareFlags & PERFECTLY_QUADRI_HANDLED)
      return true;

    if(!(SquareFlags & IN_SQUARE_STACK))
    {
      Square->Flags |= IN_SQUARE_STACK;
      Stack[StackIndex++] = Square;
    }

    if(SquareFlags & IS_TRANSPARENT)
    {
      Square->Flags |= PERFECTLY_QUADRI_HANDLED;
      return true;
    }

    const int SquarePartIndex = (x & 1) + ((y & 1) << 1);
    Square->SquarePartLastSeen = (Square->SquarePartLastSeen
				 & ~SquarePartTickMask[SquarePartIndex])
				| ShiftedTick[SquarePartIndex];
    return false;
  }
  static ulong& GetTickReference(int X, int Y)
  {
    return Map[X][Y]->SquarePartLastSeen;
  }
  static void ProcessStack()
  {
    for(long c = 0; c < StackIndex; ++c)
      Stack[c]->SignalSeen(Tick);
  }
};

void level::UpdateLOS()
{
  game::RemoveLOSUpdateRequest();
  stackcontroller::Map = Map;
  stackcontroller::Stack = SquareStack;
  stackcontroller::StackIndex = 0;
  tickcontroller::Tick = game::IncreaseLOSTick();
  tickcontroller::PrepareShiftedTick();
  int Radius = PLAYER->GetLOSRange();

  for(int c = 0; c < PLAYER->GetSquaresUnder(); ++c)
    mapmath<loscontroller>::DoQuadriArea(PLAYER->GetPos(c).X, PLAYER->GetPos(c).Y,
					 Radius * Radius, XSize, YSize);

  loscontroller::ProcessStack();

  if(PLAYER->StateIsActivated(INFRA_VISION))
    for(int c = 0; c < game::GetTeams(); ++c)
      for(std::list<character*>::const_iterator i = game::GetTeam(c)->GetMember().begin(); i != game::GetTeam(c)->GetMember().end(); ++i)
	if((*i)->IsEnabled())
	  (*i)->SendNewDrawRequest();
}

void level::EnableGlobalRain()
{
  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
      Map[x][y]->EnableGlobalRain();
}

void level::DisableGlobalRain()
{
  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
      Map[x][y]->DisableGlobalRain();
}

/* --------------------------------------------------------------------------
   Wilderness weather
   --------------------------------------------------------------------------
   Every enterable wilderness biome owns a small cycle of clear and
   precipitation spells: a biome-suited type (rain or snow), a per-state
   intensity, and spell lengths drawn from a persisted stream. The state lives
   on the level and is serialized, so a save, an autosave, a refusal followed by
   a real entry, or a leave-and-return resumes the same spell. A tile derives
   its own stream from its identity, so two jungles do not change together.
   The precipitation stays visual: it never spills standing liquid and never
   repaints terrain, which keeps a long spell from flooding the map. */

enum weathermaterial { WEATHER_NO_MATERIAL = 0, WEATHER_RAIN, WEATHER_SNOW };

/* Times are game ticks on the same clock the scripted town rain uses, so a
   spell is a meaningful stretch of play rather than a real-time second. */
struct weatherprofile
{
  int Material;                        /* WEATHER_RAIN or WEATHER_SNOW */
  int Weight[WEATHER_STATE_COUNT];     /* relative chance of entering a state */
  int Volume[WEATHER_STATE_COUNT];     /* liquid volume while in the state */
  int MinTicks[WEATHER_STATE_COUNT];   /* spell length range, in game ticks */
  int MaxTicks[WEATHER_STATE_COUNT];
  int Wind;                            /* horizontal drift of the drops */
  int Fall;                            /* downward speed of the drops */
  int HeavyWindBonus;                  /* extra drift while heavy */
};

/* A level script type is a wilderness biome exactly when the generator
   dispatches it to GenerateWilderness(). */
truth IsWeatherBiome(int Type)
{
  return Type >= DESERT && Type <= OCEAN_LEVEL;
}

weatherprofile WildernessWeatherProfile(int Type)
{
  weatherprofile W = {};
  W.Material = WEATHER_RAIN;
  W.Wind = 120;
  W.Fall = 256;

  switch(Type)
  {
   case JUNGLE:
     W.Weight[WEATHER_CLEAR] = 30;
     W.Weight[WEATHER_LIGHT] = 55;
     W.Weight[WEATHER_HEAVY] = 15;
     W.Volume[WEATHER_LIGHT] = 90;
     W.Volume[WEATHER_HEAVY] = 200;
     W.MinTicks[WEATHER_CLEAR] = 1500; W.MaxTicks[WEATHER_CLEAR] = 3600;
     W.MinTicks[WEATHER_LIGHT] = 900; W.MaxTicks[WEATHER_LIGHT] = 2200;
     W.MinTicks[WEATHER_HEAVY] = 400; W.MaxTicks[WEATHER_HEAVY] = 1100;
     W.Wind = 90;
     W.HeavyWindBonus = 60;
     break;

   case LEAFY_FOREST:
     W.Weight[WEATHER_CLEAR] = 45;
     W.Weight[WEATHER_LIGHT] = 45;
     W.Weight[WEATHER_HEAVY] = 10;
     W.Volume[WEATHER_LIGHT] = 70;
     W.Volume[WEATHER_HEAVY] = 150;
     W.MinTicks[WEATHER_CLEAR] = 1800; W.MaxTicks[WEATHER_CLEAR] = 4200;
     W.MinTicks[WEATHER_LIGHT] = 900; W.MaxTicks[WEATHER_LIGHT] = 2400;
     W.MinTicks[WEATHER_HEAVY] = 500; W.MaxTicks[WEATHER_HEAVY] = 1300;
     W.Wind = 110;
     W.HeavyWindBonus = 50;
     break;

   case EVERGREEN_FOREST:
     W.Material = WEATHER_SNOW;
     W.Weight[WEATHER_CLEAR] = 40;
     W.Weight[WEATHER_LIGHT] = 50;
     W.Weight[WEATHER_HEAVY] = 10;
     W.Volume[WEATHER_LIGHT] = 70;
     W.Volume[WEATHER_HEAVY] = 150;
     W.MinTicks[WEATHER_CLEAR] = 1800; W.MaxTicks[WEATHER_CLEAR] = 4200;
     W.MinTicks[WEATHER_LIGHT] = 1000; W.MaxTicks[WEATHER_LIGHT] = 2600;
     W.MinTicks[WEATHER_HEAVY] = 600; W.MaxTicks[WEATHER_HEAVY] = 1500;
     W.Wind = -80;
     W.Fall = 128;
     W.HeavyWindBonus = -40;
     break;

   case STEPPE:
     W.Weight[WEATHER_CLEAR] = 70;
     W.Weight[WEATHER_LIGHT] = 28;
     W.Weight[WEATHER_HEAVY] = 2;
     W.Volume[WEATHER_LIGHT] = 45;
     W.Volume[WEATHER_HEAVY] = 90;
     W.MinTicks[WEATHER_CLEAR] = 2400; W.MaxTicks[WEATHER_CLEAR] = 6000;
     W.MinTicks[WEATHER_LIGHT] = 800; W.MaxTicks[WEATHER_LIGHT] = 2000;
     W.MinTicks[WEATHER_HEAVY] = 400; W.MaxTicks[WEATHER_HEAVY] = 900;
     W.Wind = 140;
     W.HeavyWindBonus = 80;
     break;

   case DESERT:
     W.Weight[WEATHER_CLEAR] = 94;
     W.Weight[WEATHER_LIGHT] = 6;
     W.Weight[WEATHER_HEAVY] = 0;
     W.Volume[WEATHER_LIGHT] = 35;
     W.Volume[WEATHER_HEAVY] = 0;
     W.MinTicks[WEATHER_CLEAR] = 3000; W.MaxTicks[WEATHER_CLEAR] = 8000;
     W.MinTicks[WEATHER_LIGHT] = 500; W.MaxTicks[WEATHER_LIGHT] = 1400;
     W.MinTicks[WEATHER_HEAVY] = 0; W.MaxTicks[WEATHER_HEAVY] = 0;
     W.Wind = 90;
     W.HeavyWindBonus = 0;
     break;

   case TUNDRA:
     W.Material = WEATHER_SNOW;
     W.Weight[WEATHER_CLEAR] = 45;
     W.Weight[WEATHER_LIGHT] = 50;
     W.Weight[WEATHER_HEAVY] = 5;
     W.Volume[WEATHER_LIGHT] = 60;
     W.Volume[WEATHER_HEAVY] = 130;
     W.MinTicks[WEATHER_CLEAR] = 1800; W.MaxTicks[WEATHER_CLEAR] = 4400;
     W.MinTicks[WEATHER_LIGHT] = 1000; W.MaxTicks[WEATHER_LIGHT] = 2600;
     W.MinTicks[WEATHER_HEAVY] = 600; W.MaxTicks[WEATHER_HEAVY] = 1600;
     W.Wind = -60;
     W.Fall = 128;
     W.HeavyWindBonus = -40;
     break;

   case GLACIER:
     W.Material = WEATHER_SNOW;
     W.Weight[WEATHER_CLEAR] = 35;
     W.Weight[WEATHER_LIGHT] = 45;
     W.Weight[WEATHER_HEAVY] = 20;
     W.Volume[WEATHER_LIGHT] = 80;
     W.Volume[WEATHER_HEAVY] = 190;
     W.MinTicks[WEATHER_CLEAR] = 1500; W.MaxTicks[WEATHER_CLEAR] = 3800;
     W.MinTicks[WEATHER_LIGHT] = 900; W.MaxTicks[WEATHER_LIGHT] = 2400;
     W.MinTicks[WEATHER_HEAVY] = 500; W.MaxTicks[WEATHER_HEAVY] = 1400;
     W.Wind = 160;
     W.Fall = 128;
     W.HeavyWindBonus = 90;
     break;

   case OCEAN_LEVEL:
     W.Weight[WEATHER_CLEAR] = 40;
     W.Weight[WEATHER_LIGHT] = 45;
     W.Weight[WEATHER_HEAVY] = 15;
     W.Volume[WEATHER_LIGHT] = 80;
     W.Volume[WEATHER_HEAVY] = 190;
     W.MinTicks[WEATHER_CLEAR] = 1600; W.MaxTicks[WEATHER_CLEAR] = 4000;
     W.MinTicks[WEATHER_LIGHT] = 900; W.MaxTicks[WEATHER_LIGHT] = 2400;
     W.MinTicks[WEATHER_HEAVY] = 500; W.MaxTicks[WEATHER_HEAVY] = 1400;
     W.Wind = 160;
     W.Fall = 300;
     W.HeavyWindBonus = 90;
     break;

   default:
     W.Material = WEATHER_NO_MATERIAL;
     break;
  }

  return W;
}

/* Small deterministic stream, so a tile's schedule never marches in step with
   its neighbours' or with the shared game RNG. */
int NextWeatherRandom(ulong& Stream)
{
  Stream = Stream * 1103515245UL + 12345UL;
  return int((Stream >> 16) & 0x7fff);
}

int RollWeatherState(const weatherprofile& W, ulong& Stream)
{
  int Total = W.Weight[WEATHER_CLEAR] + W.Weight[WEATHER_LIGHT]
    + W.Weight[WEATHER_HEAVY];

  if(Total <= 0)
    return WEATHER_CLEAR;

  int Roll = NextWeatherRandom(Stream) % Total;

  for(int s = 0; s < WEATHER_STATE_COUNT; ++s)
  {
    if(Roll < W.Weight[s])
      return s;

    Roll -= W.Weight[s];
  }

  return WEATHER_CLEAR;
}

long RollWeatherDuration(const weatherprofile& W, int Which, ulong& Stream)
{
  int Min = W.MinTicks[Which];
  int Max = W.MaxTicks[Which];

  if(Max <= Min)
    return Min;

  return Min + NextWeatherRandom(Stream) % (Max - Min + 1);
}

/* A calm spell is a gentle drift, never a zero vector: the drop code divides by
   the speed's magnitude. */
v2 WeatherSpeedFor(const weatherprofile& W, int Which)
{
  int Wind = W.Wind + (Which == WEATHER_HEAVY ? W.HeavyWindBonus : 0);
  return v2(Wind, Max(1, W.Fall));
}

void level::InitializeWeather()
{
  int Type = LevelScript->GetType() ? *LevelScript->GetType() : 0;
  WeatherEnabled = IsWeatherBiome(Type);

  if(!WeatherEnabled)
    return;

  weatherprofile W = WildernessWeatherProfile(Type);

  /* Seed from the level's own identity, not the shared RNG. */
  WeatherRandomState = (Dungeon ? ulong(Dungeon->GetIndex()) : 0) * 65537UL
    + ulong(Index);
  WeatherRandomState ^= WeatherRandomState >> 16;
  WeatherRandomState *= 0x7feb352dUL;
  WeatherRandomState ^= WeatherRandomState >> 15;
  WeatherRandomState *= 0x846ca68bUL;
  WeatherRandomState ^= WeatherRandomState >> 16;

  WeatherState = RollWeatherState(W, WeatherRandomState);
  WeatherTimer = RollWeatherDuration(W, WeatherState, WeatherRandomState);

  /* The material and phase exist from generation, but dormant: the volume is
     zero, so a refused first entry is written out quietly and the spell only
     starts falling once somebody is actually there. */
  if(W.Material == WEATHER_SNOW)
    CreateGlobalRain(powder::Spawn(SNOW), WeatherSpeedFor(W, WeatherState));
  else
    CreateGlobalRain(liquid::Spawn(WATER), WeatherSpeedFor(W, WeatherState));

  if(GlobalRainLiquid)
    GlobalRainLiquid->SetVolumeNoSignals(0);
}

void level::ApplyWeatherState()
{
  if(!WeatherEnabled || !GlobalRainLiquid || !LevelScript->GetType())
    return;

  weatherprofile W = WildernessWeatherProfile(*LevelScript->GetType());
  long NewVolume = W.Volume[WeatherState];
  long OldVolume = GlobalRainLiquid->GetVolume();

  if(NewVolume && !OldVolume)
    EnableGlobalRain();
  else if(!NewVolume && OldVolume)
    DisableGlobalRain();

  GlobalRainLiquid->SetVolumeNoSignals(NewVolume);

  /* Push the wind into the active binding and into every existing drop, since
     setting only the game binding leaves old rain objects at their old speed. */
  SetGlobalRainSpeed(WeatherSpeedFor(W, WeatherState));
}

void level::UpdateWeather()
{
  if(!WeatherEnabled || !LevelScript->GetType())
    return;

  /* A resumed map reloads its precipitation material with zero volume; the
     first time the clock runs, re-apply the stored spell so a wet state becomes
     visible again instead of waiting for the next transition. This is cheap and
     only ever fires once after a load. */
  if(GlobalRainLiquid && WeatherState != WEATHER_CLEAR
     && !GlobalRainLiquid->GetVolume())
    ApplyWeatherState();

  if(WeatherTimer > 0)
  {
    --WeatherTimer;
    return;
  }

  weatherprofile W = WildernessWeatherProfile(*LevelScript->GetType());
  WeatherState = RollWeatherState(W, WeatherRandomState);
  WeatherTimer = RollWeatherDuration(W, WeatherState, WeatherRandomState);
  ApplyWeatherState();
}

void level::SetGlobalRainSpeed(v2 Speed)
{
  GlobalRainSpeed = Speed;

  if(game::GetGlobalRainLiquid() && game::GetGlobalRainLiquid() == GlobalRainLiquid)
    game::SetGlobalRainSpeed(Speed);

  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
      Map[x][y]->SetGlobalRainSpeed(Speed);
}

#ifdef WILDERNESS_TEST_HARNESS
void level::ForceWeatherForTest(int State, long Duration)
{
  WeatherState = State;
  WeatherTimer = Duration;
  ApplyWeatherState();
}

void level::GetWeatherDurationBoundsForTest(int State, int& Min, int& Max) const
{
  weatherprofile W = WildernessWeatherProfile(LevelScript->GetType()
					      ? *LevelScript->GetType() : 0);
  Min = W.MinTicks[State];
  Max = W.MaxTicks[State];
}

int level::GetWeatherStateVolumeForTest(int State) const
{
  weatherprofile W = WildernessWeatherProfile(LevelScript->GetType()
					      ? *LevelScript->GetType() : 0);
  return W.Volume[State];
}

void level::TickWeatherForTest(int Times)
{
  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
      Map[x][y]->TickRainsForTest(Times);
}

truth level::GlobalRainsHaveSpeedForTest(v2 Speed) const
{
  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
      if(!Map[x][y]->GlobalRainsHaveSpeedForTest(Speed))
	return false;

  return true;
}
#endif

void level::InitLastSeen()
{
  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
      Map[x][y]->InitLastSeen();
}

void level::EmitSunBeams()
{
  stackcontroller::Map = Map;
  stackcontroller::LevelXSize = XSize;
  stackcontroller::LevelYSize = YSize;
  sunbeamcontroller::ReSunEmitation = false;
  v2 Dir = SunLightDirection;
  int x, y, X = 0, Y = 0, SourceFlags;
  ulong IDFlags;

  /* Do not try to understand the logic behind the starting points of
     sunbeams. I determined the formulas by trial and error since all
     understandable loops produced strange shapes for the shadows of
     either small of large objects probably due to rounding errors
     made during line calculations. */

  if(!Dir.X || (Dir.Y && abs(Dir.Y) < abs(Dir.X)))
  {
    if(Dir.Y > 0)
    {
      Y = -1;
      SourceFlags = SP_BOTTOM;
      IDFlags = ID_X_COORDINATE|ID_BEGIN;
    }
    else
    {
      Y = YSize;
      SourceFlags = SP_TOP;
      IDFlags = ID_X_COORDINATE;
    }
  }
  else
  {
    if(Dir.X > 0)
    {
      X = -1;
      SourceFlags = SP_RIGHT;
      IDFlags = ID_BEGIN;
    }
    else
    {
      X = XSize;
      SourceFlags = SP_LEFT;
      IDFlags = 0;
    }
  }

  if(!Dir.X)
  {
    int Index = XSize << 3;

    for(x = 0; x < XSize; ++x, ++Index)
      EmitSunBeam(v2(x, Y), Index | IDFlags, SourceFlags);
  }
  else if(!Dir.Y)
  {
    int Index = YSize << 3;

    for(y = 0; y < YSize; ++y, ++Index)
      EmitSunBeam(v2(X, y), Index | IDFlags, SourceFlags);
  }
  else if(abs(Dir.Y) < abs(Dir.X))
  {
    int Index = Dir.X > 0 ? 0 : XSize << 3;
    int StartX = Dir.X > 0 ? -XSize << 3 : 0;
    int EndX = Dir.X > 0 ? XSize : (XSize << 3) + XSize;

    for(x = StartX; x < EndX; ++x, ++Index)
      EmitSunBeam(v2(x, Y), Index | IDFlags, SourceFlags);
  }
  else
  {
    int Index = Dir.Y > 0 ? 0 : YSize << 3;
    int StartY = Dir.Y > 0 ? -YSize << 3 : 0;
    int EndY = Dir.Y > 0 ? YSize : (YSize << 3) + YSize;

    for(y = StartY; y < EndY; ++y, ++Index)
      EmitSunBeam(v2(X, y), Index | IDFlags, SourceFlags);
  }
}

void level::EmitSunBeam(v2 S, ulong ID, int SourceFlags) const
{
  S <<= 1;
  v2 D = S + SunLightDirection;
  sunbeamcontroller::ID = ID;

  if(SourceFlags & SP_TOP_LEFT)
  {
    sunbeamcontroller::SunLightBlockHeight = 0;
    mapmath<sunbeamcontroller>::DoLine(S.X,     S.Y,     D.X,     D.Y, SKIP_FIRST);
  }

  if(SourceFlags & SP_TOP_RIGHT)
  {
    sunbeamcontroller::SunLightBlockHeight = 0;
    mapmath<sunbeamcontroller>::DoLine(S.X + 1, S.Y,     D.X + 1, D.Y, SKIP_FIRST);
  }

  if(SourceFlags & SP_BOTTOM_LEFT)
  {
    sunbeamcontroller::SunLightBlockHeight = 0;
    mapmath<sunbeamcontroller>::DoLine(S.X,     S.Y + 1, D.X,     D.Y + 1, SKIP_FIRST);
  }

  if(SourceFlags & SP_BOTTOM_RIGHT)
  {
    sunbeamcontroller::SunLightBlockHeight = 0;
    mapmath<sunbeamcontroller>::DoLine(S.X + 1, S.Y + 1, D.X + 1, D.Y + 1, SKIP_FIRST);
  }
}

truth sunbeamcontroller::Handler(int x, int y)
{
  int X = x >> 1, Y = y >> 1;

  if(X < 0 || Y < 0 || X >= LevelXSize || Y >= LevelYSize)
    return (X >= -1 && X <= LevelXSize) || (Y >= -1 && Y <= LevelYSize);

  lsquare* Square = Map[X][Y];
  int SquarePartIndex = (x & 1) + ((y & 1) << 1);

  if(SunLightBlockHeight && !Square->IsInside()
     && HypotSquare(x - SunLightBlockPos.X, y - SunLightBlockPos.Y) > SunLightBlockHeight)
    SunLightBlockHeight = 0;

  if(!SunLightBlockHeight)
  {
    ulong Flag = 1 << EMITTER_SQUARE_PART_SHIFT << SquarePartIndex;
    Square->AddSunLightEmitter(ID | Flag);
  }
  else
  {
    ulong Flags = ((1 << EMITTER_SQUARE_PART_SHIFT)
		   | (1 << EMITTER_SHADOW_SHIFT))
		      << SquarePartIndex;

    Square->AddSunLightEmitter(ID | Flags);
  }

  if(ReSunEmitation)
  {
    if(!(Square->Flags & IN_SQUARE_STACK))
      Stack[StackIndex++] = Square;

    Square->Flags |= IN_SQUARE_STACK|CHECK_SUN_LIGHT_NEEDED;

    for(int d = 0; d < 8; ++d)
    {
      lsquare* Neighbour = Square->GetNeighbourLSquare(d);

      if(Neighbour && !(Neighbour->Flags & IN_SQUARE_STACK))
      {
	Neighbour->Flags |= IN_SQUARE_STACK;
	Stack[StackIndex++] = Neighbour;
      }
    }
  }

  if(!(Square->Flags & IS_TRANSPARENT) || (SunLightBlockHeight && Square->IsInside()))
  {
    /* This should depend on the square */
    SunLightBlockHeight = 81;
    SunLightBlockPos = v2(x, y);
  }

  return true;
}

void sunbeamcontroller::ProcessStack()
{
  long c;

  for(c = 0; c < StackIndex; ++c)
  {
    lsquare* Square = Stack[c];

    if(Square->Flags & CHECK_SUN_LIGHT_NEEDED)
    {
      if(Square->Flags & IS_TRANSPARENT)
	Square->CalculateSunLightLuminance(EMITTER_SQUARE_PART_BITS);

      Square->SendSunLightSignals();
      Square->ZeroReSunEmitatedFlags();
    }

    Square->Flags &= ~(IN_SQUARE_STACK|CHECK_SUN_LIGHT_NEEDED);
  }

  for(c = 0; c < StackIndex; ++c)
    Stack[c]->CheckIfIsSecondarySunLightEmitter();
}

int level::DetectMaterial(const material* Material)
{
  ulong Tick = game::IncreaseLOSTick();
  int Squares = 0;

  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
    {
      lsquare* Square = Map[x][y];

      if(Square->DetectMaterial(Material))
      {
	Square->Reveal(Tick, true);
	++Squares;
      }
    }

  return Squares;
}

void level::BlurMemory()
{
  int x, y, SquareStackSize = 0;

  for(x = 0; x < XSize; ++x)
    for(y = 0; y < YSize; ++y)
    {
      lsquare* Square = Map[x][y];

      if(Square->HasNoBorderPartners())
	SquareStack[SquareStackSize++] = Square;
    }

  for(x = 0; x < XSize; ++x)
    for(y = 0; y < YSize; ++y)
    {
      lsquare* Square = Map[x][y];
      Square->Flags |= STRONG_NEW_DRAW_REQUEST
		       | MEMORIZED_UPDATE_REQUEST
		       | DESCRIPTION_CHANGE;

      if(Square->HasNoBorderPartners()
	 && RAND() & 1
	 && SquareStackSize)
	Square->SwapMemorized(SquareStack[RAND() % SquareStackSize]);
      else if(RAND() & 1)
	Square->DestroyMemorized();
    }
}

void level::CalculateLuminances()
{
  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
    {
      lsquare* Square = Map[x][y];
      Square->CalculateLuminance();
      Square->Flags |= MEMORIZED_UPDATE_REQUEST
		       | DESCRIPTION_CHANGE;
    }
}

struct areacontroller : public stackcontroller
{
  static truth Handler(int x, int y)
  {
    if(x >= 0 && y >= 0 && x < LevelXSize && y < LevelYSize
       && HypotSquare(x - Center.X, y - Center.Y) <= RadiusSquare)
    {
      lsquare* Square = Map[x][y];

      if(!(Square->Flags & IN_SQUARE_STACK))
      {
	Stack[StackIndex++] = Square;
	Square->Flags |= IN_SQUARE_STACK;
	return Square->IsFlyable();
      }
    }

    return false;
  }
  static int GetStartX(int) { return Center.X; }
  static int GetStartY(int) { return Center.Y; }
  static long RadiusSquare;
};

long areacontroller::RadiusSquare;

int level::AddRadiusToSquareStack(v2 Center, long RadiusSquare) const
{
  stackcontroller::Map = Map;
  stackcontroller::Stack = SquareStack;
  SquareStack[0] = GetLSquare(Center);
  stackcontroller::StackIndex = 1;
  stackcontroller::LevelXSize = XSize;
  stackcontroller::LevelYSize = YSize;
  stackcontroller::Center = Center;
  areacontroller::RadiusSquare = RadiusSquare;
  mapmath<areacontroller>::DoArea();
  return stackcontroller::StackIndex;
}

/* Any fountain is good that is not dry and is NOT Except */

olterrain* level::GetRandomFountainWithWater(olterrain* Except) const
{
  std::vector<olterrain*> Found;
  olterrain* OLTerrain;
  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
    {
      OLTerrain = GetLSquare(x,y)->GetOLTerrain();
      if(OLTerrain && OLTerrain != Except && OLTerrain->IsFountainWithWater())
	Found.push_back(OLTerrain);
    }

  if(Found.empty())
    return 0;

  return Found[RAND_N(Found.size())];
}

void level::Amnesia(int Percentile)
{
  for(int x = 0; x < XSize; ++x)
    for(int y = 0; y < YSize; ++y)
    {
      lsquare* Square = Map[x][y];

      if(Square->HasNoBorderPartners() && RAND_N(100) < Percentile)
      {
	Square->Flags |= STRONG_NEW_DRAW_REQUEST
			 | MEMORIZED_UPDATE_REQUEST
			 | DESCRIPTION_CHANGE;

	Square->DestroyMemorized();
      }
    }
}

/* Returns how many of the monsters were seen */

spawnresult level::SpawnMonsters(characterspawner Spawner, team* Team,
				 v2 Pos, int Config, int Amount,
				 truth IgnoreWalkability)
{
  spawnresult SR = { 0, 0 };

  for(int c = 0; c < Amount; ++c)
  {
    character* Char = Spawner(Config, 0);

    if(!c)
      SR.Pioneer = Char;

    Char->SetTeam(Team);

    if(IgnoreWalkability)
      Char->ForcePutNear(Pos);
    else
      Char->PutNear(Pos);

    if(Char->CanBeSeenByPlayer())
      ++SR.Seen;
  }

  return SR;
}
