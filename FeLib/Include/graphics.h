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

#ifndef __GRAPHICS_H__
#define __GRAPHICS_H__

#include "v2.h"

#define DOUBLE_BUFFER graphics::GetDoubleBuffer()
#define RES graphics::GetRes()
#define FONT graphics::GetDefaultFont()

class bitmap;
class rawbitmap;
class festring;

/* Facade over the video backend. The platform-dependent definitions of
   Init, DeInit, SetMode, BlitDBToScreen and SwitchMode live in exactly one
   folder under FeLib/Source/backends/, selected by the build system. */

class graphics
{
 public:
  friend class bitmap;
  static void Init();
  static void DeInit();
  static void SwitchMode();
  static void SetMode(const char*, const char*, v2, truth);
  /* Desktop size and video mode availability, used to choose the initial
     game resolution (see igraph::Init()). Implemented by the backend. */
  static v2 GetDesktopRes();
  static truth IsModeSupported(v2);
  static void BlitDBToScreen();
  static v2 GetRes() { return Res; }
  static bitmap* GetDoubleBuffer() { return DoubleBuffer; }
  static void LoadDefaultFont(const festring&);
  static rawbitmap* GetDefaultFont() { return DefaultFont; }
  static void SetSwitchModeHandler(void (*What)())
  { SwitchModeHandler = What; }
 private:
  static void (*SwitchModeHandler)();
  static bitmap* DoubleBuffer;
  static v2 Res;
  static int ColorDepth;
  static rawbitmap* DefaultFont;
};

#endif
