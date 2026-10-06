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

#ifdef USE_SDL
#include "SDL.h"
#endif

#include "v2.h"

#define DOUBLE_BUFFER graphics::GetDoubleBuffer()
#define RES graphics::GetRes()
#define FONT graphics::GetDefaultFont()

class bitmap;
class rawbitmap;
class festring;

class graphics
{
 public:
  friend class bitmap;
  static void Init();
  static void DeInit();
#ifdef USE_SDL
  static void SwitchMode();
#endif
#ifdef __DJGPP__
  static void SwitchMode() { }
#endif
  static void SetMode(const char*, const char*, v2, truth);
  static void BlitDBToScreen();
  static v2 GetRes() { return Res; }
  static bitmap* GetDoubleBuffer() { return DoubleBuffer; }
  static void LoadDefaultFont(const festring&);
  static rawbitmap* GetDefaultFont() { return DefaultFont; }
  static void SetSwitchModeHandler(void (*What)())
  { SwitchModeHandler = What; }
 private:
  static void (*SwitchModeHandler)();
#ifdef USE_SDL
  static SDL_Surface* Screen;
#endif
#ifdef __DJGPP__
  static ulong BufferSize;
  static ushort ScreenSelector;
  static struct vesainfo
  {
    void Retrieve();
    ulong Signature NO_ALIGNMENT;
    ushort Version NO_ALIGNMENT;
    ulong OEMString NO_ALIGNMENT;
    ulong Capabilities NO_ALIGNMENT;
    ulong ModeList NO_ALIGNMENT;
    ushort Memory NO_ALIGNMENT;
    uchar Shit[493];
  } VesaInfo;
  static struct modeinfo
  {
    void Retrieve(ushort);
    ushort Attribs1 NO_ALIGNMENT;
    uchar AWindowAttribs;
    uchar BWindowAttribs;
    ushort Granularity NO_ALIGNMENT;
    ushort WindowSize NO_ALIGNMENT;
    ushort WindowASegment NO_ALIGNMENT;
    ushort WindowBSegment NO_ALIGNMENT;
    ulong WindowMoveFunction NO_ALIGNMENT;
    ushort BytesPerLine NO_ALIGNMENT;
    ushort Width NO_ALIGNMENT;
    ushort Height NO_ALIGNMENT;
    uchar CharWidth;
    uchar CharHeight;
    uchar Planes;
    uchar BitsPerPixel;
    uchar Banks;
    uchar MemoryModel;
    uchar BankSize;
    uchar ImagePages;
    uchar Reserved1;
    uchar RedBits;
    uchar RedShift;
    uchar GreenBits;
    uchar GreenShift;
    uchar BlueBits;
    uchar BlueShift;
    uchar ResBits;
    uchar ResShift;
    uchar Attribs2;
    ulong PhysicalLFBAddress NO_ALIGNMENT;
    ulong OffScreenMem NO_ALIGNMENT;
    ushort OffScreenMemSize NO_ALIGNMENT;
    uchar Reserved2[206];
  } ModeInfo;
#endif
  static bitmap* DoubleBuffer;
  static v2 Res;
  static int ColorDepth;
  static rawbitmap* DefaultFont;
};

#endif
