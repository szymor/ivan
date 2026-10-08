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

/* SDL2 video backend: window management and presenting the software-
   rendered double buffer through an SDL2 renderer. The game always hands
   us a 16-bit RGB565 buffer (packcol16), which is streamed into a texture
   and copied to the window (scaled to the logical resolution). */

#include "SDL.h"

#include <cstdlib>

#include "graphics.h"
#include "bitmap.h"
#include "whandler.h"
#include "error.h"
#include "rawbit.h"

static SDL_Window* Window;
static SDL_Renderer* Renderer;
static SDL_Texture* Texture;

void graphics::Init()
{
  static truth AlreadyInstalled = false;

  if(!AlreadyInstalled)
  {
    AlreadyInstalled = true;

    /* SDL_INIT_NOPARACHUTE does not exist in SDL2. */
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER))
      ABORT("Can't initialize SDL.");

    /* The 800x600 buffer rarely maps 1:1 onto the screen (e.g. 1366x768
       fullscreen is 1.28x from the desktop). Nearest-neighbour scaling of
       a non-integer factor doubles pixels unevenly, so request linear
       interpolation for the final present (applies to textures created
       from here on; the streaming texture also sets it explicitly). */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    atexit(graphics::DeInit);
  }
}

void graphics::DeInit()
{
  delete DefaultFont;
  DefaultFont = 0;

  if(Texture)
  {
    SDL_DestroyTexture(Texture);
    Texture = 0;
  }

  if(Renderer)
  {
    SDL_DestroyRenderer(Renderer);
    Renderer = 0;
  }

  if(Window)
  {
    SDL_DestroyWindow(Window);
    Window = 0;
  }

  SDL_Quit();
}

void graphics::SetMode(const char* Title, const char* IconName,
		       v2 NewRes, truth FullScreen)
{
  ulong Flags = 0;

  if(FullScreen)
  {
    SDL_ShowCursor(SDL_DISABLE);
    Flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  }
  else
    SDL_ShowCursor(SDL_ENABLE);

  Window = SDL_CreateWindow(Title, SDL_WINDOWPOS_UNDEFINED,
			    SDL_WINDOWPOS_UNDEFINED,
			    NewRes.X, NewRes.Y, Flags);

  if(!Window)
    ABORT("Couldn't create window.");

  if(IconName)
  {
    SDL_Surface* Icon = SDL_LoadBMP(IconName);

    if(Icon)
    {
      /* SDL2's window-icon code (the X11 driver in particular) requires an
	 ARGB8888 surface; convert first and free both surfaces afterwards,
	 as the icon is copied by SDL_SetWindowIcon. */
      SDL_Surface* ArgIcon = SDL_ConvertSurfaceFormat(Icon,
						      SDL_PIXELFORMAT_ARGB8888, 0);

      if(ArgIcon)
      {
	SDL_SetWindowIcon(Window, ArgIcon);
	SDL_FreeSurface(ArgIcon);
      }

      SDL_FreeSurface(Icon);
    }
  }

  /* Request a vsynced accelerated renderer, but fall back silently to a
     plain accelerated or software renderer -- the game does not depend
     on vsync. */
  Renderer = SDL_CreateRenderer(Window, -1,
				SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

  if(!Renderer)
    Renderer = SDL_CreateRenderer(Window, -1, SDL_RENDERER_ACCELERATED);

  if(!Renderer)
    Renderer = SDL_CreateRenderer(Window, -1, 0);

  if(!Renderer)
    ABORT("Couldn't create renderer.");

  SDL_RenderSetLogicalSize(Renderer, NewRes.X, NewRes.Y);

  Texture = SDL_CreateTexture(Renderer, SDL_PIXELFORMAT_RGB565,
			      SDL_TEXTUREACCESS_STREAMING,
			      NewRes.X, NewRes.Y);

  if(!Texture)
    ABORT("Couldn't create texture.");

  /* Explicit scale mode -- the hint above already made this the default,
     but state it here so the smoothing does not depend on hint handling. */
  SDL_SetTextureScaleMode(Texture, SDL_ScaleModeLinear);

  globalwindowhandler::Init();
  DoubleBuffer = new bitmap(NewRes);
  Res = NewRes;
  ColorDepth = 16;
}

v2 graphics::GetDesktopRes()
{
  SDL_DisplayMode Mode;

  if(SDL_GetDesktopDisplayMode(0, &Mode))
    return v2(0, 0);

  return v2(Mode.w, Mode.h);
}

/* The renderer scales any logical resolution to the window, so every
   mode is usable. */
truth graphics::IsModeSupported(v2)
{
  return true;
}

void graphics::BlitDBToScreen()
{
  /* Row pitch is two bytes per pixel (RGB565). */
  if(SDL_UpdateTexture(Texture, 0, DoubleBuffer->GetImage()[0], Res.X << 1) != 0)
    ABORT("Can't update texture.");

  if(SDL_RenderClear(Renderer) != 0)
    ABORT("Can't clear renderer.");

  if(SDL_RenderCopy(Renderer, Texture, 0, 0) != 0)
    ABORT("Can't render texture.");

  SDL_RenderPresent(Renderer);
}

void graphics::SwitchMode()
{
  truth FullScreen = SDL_GetWindowFlags(Window) & SDL_WINDOW_FULLSCREEN;

  SDL_ShowCursor(FullScreen ? SDL_ENABLE : SDL_DISABLE);

  if(SwitchModeHandler)
    SwitchModeHandler();

  if(SDL_SetWindowFullscreen(Window,
			     FullScreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
    ABORT("Couldn't toggle fullscreen mode.");

  BlitDBToScreen();
}