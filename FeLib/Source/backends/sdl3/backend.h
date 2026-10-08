#ifndef __IVAN_SDL3_BACKEND_H__
#define __IVAN_SDL3_BACKEND_H__

/* Internal to the SDL3 backend (video.cpp + input.cpp are separate
   translation units, so the window handle they both need is shared through
   this variable; nothing outside this folder references it). */

#include <SDL3/SDL.h>

extern SDL_Window* feSDLWindow;

#endif