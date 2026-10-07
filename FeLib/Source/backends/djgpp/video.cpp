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

/* DJGPP/DOS video backend: VESA 2.0 mode setting and direct
   linear-framebuffer blitting of the double buffer. */

#include <dpmi.h>
#include <conio.h>
#include <go32.h>
#include <cstdlib>

#include "graphics.h"
#include "bitmap.h"
#include "error.h"
#include "rawbit.h"

static ulong BufferSize;
static ushort ScreenSelector = 0;
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

void graphics::Init()
{
  static truth AlreadyInstalled = false;

  if(!AlreadyInstalled)
  {
    AlreadyInstalled = true;

    VesaInfo.Retrieve();

    atexit(graphics::DeInit);
  }
}

void graphics::DeInit()
{
  delete DefaultFont;
  DefaultFont = 0;

  if(ScreenSelector)
  {
    __dpmi_free_ldt_descriptor(ScreenSelector);
    ScreenSelector = 0;
    textmode(0x3);
  }
}

void graphics::SetMode(const char*, const char*, v2 NewRes, truth)
{
  ulong Mode;

  for(Mode = 0; Mode < 0x10000; ++Mode)
  {
    ModeInfo.Retrieve(Mode);

    if(ModeInfo.Attribs1 & 0x01
       && ModeInfo.Attribs1 & 0xFF
       && ModeInfo.Width == NewRes.X
       && ModeInfo.Height == NewRes.Y
       && ModeInfo.BitsPerPixel == 16)
      break;
  }

  if(Mode == 0x10000)
    ABORT("Resolution %dx%d not supported!", NewRes.X, NewRes.Y);

  __dpmi_regs Regs;
  Regs.x.ax = 0x4F02;
  Regs.x.bx = Mode | 0x4000;
  __dpmi_int(0x10, &Regs);
  Res.X = ModeInfo.Width;
  Res.Y = ModeInfo.Height;
  BufferSize = Res.Y * ModeInfo.BytesPerLine;
  delete DoubleBuffer;
  DoubleBuffer = new bitmap(Res);
  __dpmi_meminfo MemoryInfo;
  MemoryInfo.size = BufferSize;
  MemoryInfo.address = ModeInfo.PhysicalLFBAddress;
  __dpmi_physical_address_mapping(&MemoryInfo);
  __dpmi_lock_linear_region(&MemoryInfo);
  ScreenSelector = __dpmi_allocate_ldt_descriptors(1);
  __dpmi_set_segment_base_address(ScreenSelector, MemoryInfo.address);
  __dpmi_set_segment_limit(ScreenSelector, BufferSize - 1);
}

void graphics::BlitDBToScreen()
{
  movedata(_my_ds(), ulong(DoubleBuffer->GetImage()[0]),
	   ScreenSelector, 0, BufferSize);
}

void vesainfo::Retrieve()
{
  Signature = 0x32454256;
  dosmemput(this, sizeof(vesainfo), __tb);
  __dpmi_regs Regs;
  Regs.x.ax = 0x4F00;
  Regs.x.di =  __tb       & 0x000F;
  Regs.x.es = (__tb >> 4) & 0xFFFF;
  __dpmi_int(0x10, &Regs);
  dosmemget(__tb, sizeof(vesainfo), this);
}

void modeinfo::Retrieve(ushort Mode)
{
  __dpmi_regs Regs;
  Regs.x.ax = 0x4F01;
  Regs.x.cx = Mode;
  Regs.x.di =  __tb       & 0x000F;
  Regs.x.es = (__tb >> 4) & 0xFFFF;
  dosmemput(this, sizeof(modeinfo), __tb);
  __dpmi_int(0x10, &Regs);
  dosmemget(__tb, sizeof(modeinfo), this);
}
