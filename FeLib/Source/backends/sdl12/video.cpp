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

/* SDL 1.2 video backend: window management and presenting the
   software-rendered double buffer on the SDL screen surface. */

#include "SDL.h"

#include <cstdlib>
#include <cstring>

#include "graphics.h"
#include "bitmap.h"
#include "whandler.h"
#include "error.h"
#include "rawbit.h"

static SDL_Surface* Screen;

void graphics::Init()
{
  static truth AlreadyInstalled = false;

  if(!AlreadyInstalled)
  {
    AlreadyInstalled = true;

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_NOPARACHUTE))
      ABORT("Can't initialize SDL.");

    atexit(graphics::DeInit);
  }
}

void graphics::DeInit()
{
  delete DefaultFont;
  DefaultFont = 0;

  SDL_Quit();
}

void graphics::SetMode(const char* Title, const char* IconName,
		       v2 NewRes, truth FullScreen)
{
  if(IconName)
  {
    SDL_Surface* Icon = SDL_LoadBMP(IconName);

    if(Icon)
    {
      SDL_SetColorKey(Icon, SDL_SRCCOLORKEY,
		      SDL_MapRGB(Icon->format, 255, 255, 255));
      SDL_WM_SetIcon(Icon, NULL);
      /* SDL_WM_SetIcon copies what it needs and keeps ownership of the
	 surface with the caller, so free it here. */
      SDL_FreeSurface(Icon);
    }
  }

  ulong Flags = SDL_SWSURFACE;

  if(FullScreen)
  {
    SDL_ShowCursor(SDL_DISABLE);
    Flags |= SDL_FULLSCREEN;
  }

  Screen = SDL_SetVideoMode(NewRes.X, NewRes.Y, 16, Flags);

  if(!Screen && NewRes != v2(800, 600))
  {
    /* The mode can still fail even after IsModeSupported() approved it,
       so fall back to the legacy resolution instead of aborting. */
    NewRes = v2(800, 600);
    Screen = SDL_SetVideoMode(NewRes.X, NewRes.Y, 16, Flags);
  }

  if(!Screen)
    ABORT("Couldn't set video mode.");

  SDL_WM_SetCaption(Title, 0);
  globalwindowhandler::Init();
  DoubleBuffer = new bitmap(NewRes);
  Res = NewRes;
  ColorDepth = 16;
}

/* There is no scaler in this backend: fullscreen switches to a real
   video mode, so ask SDL for the list of modes the display accepts. */
truth graphics::IsModeSupported(v2 NewRes)
{
  const SDL_VideoInfo* Info = SDL_GetVideoInfo();

  if(!Info)
    return false;

  SDL_Rect** Modes = SDL_ListModes(Info->vfmt,
				   SDL_FULLSCREEN | SDL_SWSURFACE);

  if(Modes == (SDL_Rect**)-1) // any mode is accepted
    return true;

  for(int c = 0; Modes && Modes[c]; ++c)
    if(Modes[c]->w >= NewRes.X && Modes[c]->h >= NewRes.Y)
      return true;

  return false;
}

v2 graphics::GetDesktopRes()
{
  const SDL_VideoInfo* Info = SDL_GetVideoInfo();
  return Info ? v2(Info->current_w, Info->current_h) : v2(0, 0);
}

void graphics::BlitDBToScreen()
{
  if(SDL_MUSTLOCK(Screen) && SDL_LockSurface(Screen) < 0)
    ABORT("Can't lock screen");

  packcol16* SrcPtr = DoubleBuffer->GetImage()[0];
  packcol16* DestPtr = static_cast<packcol16*>(Screen->pixels);
  ulong ScreenYMove = (Screen->pitch >> 1);
  ulong LineSize = Res.X << 1;

  for(int y = 0; y < Res.Y; ++y, SrcPtr += Res.X, DestPtr += ScreenYMove)
    memcpy(DestPtr, SrcPtr, LineSize);

  if(SDL_MUSTLOCK(Screen))
    SDL_UnlockSurface(Screen);

  SDL_UpdateRect(Screen, 0, 0, Res.X, Res.Y);
}

void graphics::SwitchMode()
{
  ulong Flags;

  if(Screen->flags & SDL_FULLSCREEN)
  {
    SDL_ShowCursor(SDL_ENABLE);
    Flags = SDL_SWSURFACE;
  }
  else
  {
    SDL_ShowCursor(SDL_DISABLE);
    Flags = SDL_SWSURFACE|SDL_FULLSCREEN;
  }

  if(SwitchModeHandler)
    SwitchModeHandler();

  Screen = SDL_SetVideoMode(Res.X, Res.Y, ColorDepth, Flags);

  if(!Screen)
    ABORT("Couldn't toggle fullscreen mode.");

  BlitDBToScreen();
}
