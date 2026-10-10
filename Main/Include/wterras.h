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

#ifndef __WTERRAS_H__
#define __WTERRAS_H__

#include "wterra.h"
#include "ivandef.h"

GWTERRAIN(ocean, gwterrain)
{
 public:
  ocean() { SetAnimationFrames(32); }
  virtual const char* GetNameStem() const;
  virtual truth UsesLongArticle() const { return true; }
  virtual v2 GetBitmapPos(int) const;
  virtual int GetPriority() const { return 10; }
  virtual int GetAttachedDungeon() const { return WILDERNESS_OCEAN; }
  virtual const char* SurviveMessage() const;
  virtual const char* MonsterSurviveMessage() const;
  virtual const char* DeathMessage() const;
  virtual const char* MonsterDeathVerb() const;
  virtual const char* ScoreEntry() const;
  virtual truth IsFatalToStay() const { return true; }
  virtual int GetWalkability() const;
};

GWTERRAIN(glacier, gwterrain)
{
 public:
  virtual const char* GetNameStem() const;
  virtual v2 GetBitmapPos(int) const;
  virtual int GetPriority() const { return 90; }
  virtual int GetAttachedDungeon() const { return WILDERNESS_GLACIER; }
};

GWTERRAIN(desert, gwterrain)
{
 public:
  virtual const char* GetNameStem() const;
  virtual v2 GetBitmapPos(int) const;
  virtual int GetPriority() const { return 20; }
  virtual int GetAttachedDungeon() const { return WILDERNESS_DESERT; }
};

GWTERRAIN(snow, gwterrain)
{
 public:
  virtual const char* GetNameStem() const;
  virtual v2 GetBitmapPos(int) const;
  virtual int GetPriority() const { return 80; }
  virtual int GetAttachedDungeon() const { return WILDERNESS_TUNDRA; }
};

GWTERRAIN(jungle, gwterrain)
{
 public:
  virtual const char* GetNameStem() const;
  virtual v2 GetBitmapPos(int) const;
  virtual int GetPriority() const { return 50; }
  virtual int GetAttachedDungeon() const { return WILDERNESS_JUNGLE; }
};

GWTERRAIN(leafyforest, gwterrain)
{
  virtual const char* GetNameStem() const;
  virtual v2 GetBitmapPos(int) const;
  virtual int GetPriority() const { return 60; }
  virtual int GetAttachedDungeon() const { return WILDERNESS_LEAFY_FOREST; }
};

GWTERRAIN(evergreenforest, gwterrain)
{
 public:
  virtual const char* GetNameStem() const;
  virtual v2 GetBitmapPos(int) const;
  virtual truth UsesLongArticle() const { return true; }
  virtual int GetPriority() const { return 70; }
  virtual int GetAttachedDungeon() const { return WILDERNESS_EVERGREEN_FOREST; }
};

GWTERRAIN(steppe, gwterrain)
{
 public:
  virtual const char* GetNameStem() const;
  virtual v2 GetBitmapPos(int) const;
  virtual int GetPriority() const { return 30; }
  virtual int GetAttachedDungeon() const { return WILDERNESS_STEPPE; }
};

OWTERRAIN(attnam, owterrain)
{
 public:
  virtual const char* GetNameStem() const;
  virtual v2 GetBitmapPos(int) const;
  virtual int GetAttachedDungeon() const;
};

OWTERRAIN(elpuricave, owterrain)
{
 public:
  virtual const char* GetNameStem() const;
  virtual v2 GetBitmapPos(int) const;
  virtual int GetAttachedDungeon() const;
};

OWTERRAIN(newattnam, owterrain)
{
 public:
  virtual const char* GetNameStem() const;
  virtual v2 GetBitmapPos(int) const;
  virtual int GetAttachedDungeon() const;
};

OWTERRAIN(underwatertunnel, owterrain)
{
 public:
  virtual const char* GetNameStem() const;
  virtual v2 GetBitmapPos(int) const;
  virtual int GetAttachedDungeon() const;
  virtual truth UsesLongArticle() const { return true; }
};

OWTERRAIN(underwatertunnelexit, owterrain)
{
 public:
  virtual const char* GetNameStem() const;
  virtual v2 GetBitmapPos(int) const;
  virtual int GetAttachedDungeon() const;
  virtual truth UsesLongArticle() const { return true; }
  virtual int GetAttachedArea() const { return 2; }
};

#endif
