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

#include "graphics.h"
#include "rawbit.h"

/* Backend-independent part of the graphics facade. Everything that talks
   to the actual video system lives in FeLib/Source/backends/. */

void (*graphics::SwitchModeHandler)();
bitmap* graphics::DoubleBuffer;
v2 graphics::Res;
int graphics::ColorDepth;
rawbitmap* graphics::DefaultFont = 0;

void graphics::LoadDefaultFont(const festring& FileName)
{
  DefaultFont = new rawbitmap(FileName);
}
