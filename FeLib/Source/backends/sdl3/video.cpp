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

/* SDL3 video backend: window management and presenting the software-
   rendered double buffer through an SDL3 renderer. The game always hands
   us a 16-bit RGB565 buffer (packcol16), which is streamed into a texture
   and copied to the window (scaled to the logical resolution).

   Compared to the SDL2 backend:
   - SDL_CreateWindow() no longer takes a position argument.
   - SDL_CreateRenderer() has no flags argument anymore: the default
     renderer is the best accelerated driver, and vsync is requested after
     creation with SDL_SetRenderVSync() (silently optional, as before).
   - SDL_RenderSetLogicalSize() is SDL_SetRenderLogicalPresentation()
     with the letterbox mode.
   - SDL_RenderCopy() was renamed SDL_RenderTexture().
   - SDL_ShowCursor()/SDL_HideCursor() take no argument.
   - The SDL_HINT_RENDER_SCALE_QUALITY hint no longer exists; the scale
     mode is per-texture and defaults to linear anyway. */

#include <SDL3/SDL.h>

#include <cstdlib>

#include "graphics.h"
#include "bitmap.h"
#include "whandler.h"
#include "error.h"
#include "rawbit.h"
#include "backend.h"

static SDL_Window* Window;
static SDL_Renderer* Renderer;
static SDL_Texture* Texture;

/* Shared with input.cpp (see backend.h); set as soon as the window exists
   so globalwindowhandler::Init() can enable per-window text input. */
SDL_Window* feSDLWindow;

void graphics::Init()
{
  static truth AlreadyInstalled = false;

  if(!AlreadyInstalled)
  {
    AlreadyInstalled = true;

    /* SDL_INIT_TIMER/SDL_INIT_NOPARACHUTE do not exist in SDL3; video
       initialization implies the events/timer subsystems. */
    if(!SDL_Init(SDL_INIT_VIDEO))
      ABORT("Can't initialize SDL.");

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

  feSDLWindow = 0;

  SDL_Quit();
}

void graphics::SetMode(const char* Title, const char* IconName,
		       v2 NewRes, truth FullScreen)
{
  SDL_WindowFlags Flags = 0;

  if(FullScreen)
  {
    SDL_HideCursor();
    /* SDL3's SDL_WINDOW_FULLSCREEN means fullscreen at the current desktop
       resolution, i.e. what SDL 1.2/SDL2 called *_DESKTOP. */
    Flags |= SDL_WINDOW_FULLSCREEN;
  }
  else
    SDL_ShowCursor();

  feSDLWindow = Window = SDL_CreateWindow(Title, NewRes.X, NewRes.Y, Flags);

  if(!Window)
    ABORT("Couldn't create window.");

  if(IconName)
  {
    SDL_Surface* Icon = SDL_LoadBMP(IconName);

    if(Icon)
    {
      /* SDL3's video drivers convert the surface internally (the explicit
	 ARGB8888 conversion the SDL2 X11 driver asked for is not needed),
	 and SDL_SetWindowIcon copies the data, so free it here. */
      SDL_SetWindowIcon(Window, Icon);
      SDL_DestroySurface(Icon);
    }
  }

  /* SDL3 has no renderer-creation flags: the default renderer is the best
     accelerated driver. Request vsync afterwards and ignore the failure --
     the game does not depend on vsync (same silent fallback as SDL2). */
  Renderer = SDL_CreateRenderer(Window, 0);

  if(!Renderer)
    ABORT("Couldn't create renderer.");

  SDL_SetRenderVSync(Renderer, 1);

  /* Letterbox mode reproduces SDL2's SDL_RenderSetLogicalSize: the 800x600
     logical area is scaled to the window, preserving the aspect ratio. */
  if(!SDL_SetRenderLogicalPresentation(Renderer, NewRes.X, NewRes.Y,
				       SDL_LOGICAL_PRESENTATION_LETTERBOX))
    ABORT("Couldn't set logical presentation.");

  Texture = SDL_CreateTexture(Renderer, SDL_PIXELFORMAT_RGB565,
			      SDL_TEXTUREACCESS_STREAMING,
			      NewRes.X, NewRes.Y);

  if(!Texture)
    ABORT("Couldn't create texture.");

  /* The 800x600 buffer rarely maps 1:1 onto the screen (e.g. 1366x768
     fullscreen is 1.28x from the desktop). Linear interpolation of the
     fractional upscale is far more pleasant than the uneven doubled-pixel
     grid nearest-neighbour produces. (Linear is also SDL3's default, but
     state it explicitly.) */
  SDL_SetTextureScaleMode(Texture, SDL_SCALEMODE_LINEAR);

  globalwindowhandler::Init();
  DoubleBuffer = new bitmap(NewRes);
  Res = NewRes;
  ColorDepth = 16;
}

v2 graphics::GetDesktopRes()
{
  const SDL_DisplayMode* Mode =
    SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
  return Mode ? v2(Mode->w, Mode->h) : v2(0, 0);
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
  if(!SDL_UpdateTexture(Texture, 0, DoubleBuffer->GetImage()[0], Res.X << 1))
    ABORT("Can't update texture.");

  if(!SDL_RenderClear(Renderer))
    ABORT("Can't clear renderer.");

  /* SDL_RenderCopy in SDL3 is SDL_RenderTexture (float rects; NULL means
     the whole texture / the whole logical output). */
  if(!SDL_RenderTexture(Renderer, Texture, 0, 0))
    ABORT("Can't render texture.");

  SDL_RenderPresent(Renderer);
}

void graphics::SwitchMode()
{
  truth FullScreen = SDL_GetWindowFlags(Window) & SDL_WINDOW_FULLSCREEN;

  if(FullScreen)
    SDL_ShowCursor();
  else
    SDL_HideCursor();

  if(SwitchModeHandler)
    SwitchModeHandler();

  if(!SDL_SetWindowFullscreen(Window, !FullScreen))
    ABORT("Couldn't toggle fullscreen mode.");

  BlitDBToScreen();
}