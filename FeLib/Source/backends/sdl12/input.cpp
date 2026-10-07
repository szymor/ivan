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

/* SDL 1.2 input backend: event pump, key decoding and timing. */

#include "whandler.h"
#include "graphics.h"
#include "bitmap.h"
#include "festring.h"

#include "SDL.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

static std::vector<int> KeyBuffer;
static truth (*QuitMessageHandler)() = 0;

static void ProcessMessage(SDL_Event*);

void globalwindowhandler::Init()
{
  SDL_EnableUNICODE(1);
  SDL_EnableKeyRepeat(500, 30);
}

int globalwindowhandler::GetKey(truth EmptyBuffer)
{
  SDL_Event Event;

  if(EmptyBuffer)
  {
    while(SDL_PollEvent(&Event))
      ProcessMessage(&Event);

    KeyBuffer.clear();
  }

  for(;;)
    if(!KeyBuffer.empty())
    {
      int Key = KeyBuffer[0];
      KeyBuffer.erase(KeyBuffer.begin());

      if(Key > 0xE000)
	return Key - 0xE000;

      if(Key && Key < 0x81)
	return Key;
    }
    else
    {
      if(SDL_PollEvent(&Event))
	ProcessMessage(&Event);
      else
      {
	if(SDL_GetAppState() & SDL_APPACTIVE
	   && Controls && ControlLoopsEnabled)
	{
	  static ulong LastTick = 0;
	  UpdateTick();

	  if(LastTick != Tick)
	  {
	    LastTick = Tick;
	    truth Draw = false;

	    for(int c = 0; c < Controls; ++c)
	      if(ControlLoop[c]())
		Draw = true;

	    if(Draw)
	      graphics::BlitDBToScreen();
	  }

	  SDL_Delay(10);
	}
	else
	{
	  SDL_WaitEvent(&Event);
	  ProcessMessage(&Event);
	}
      }
    }
}

int globalwindowhandler::ReadKey()
{
  SDL_Event Event;

  if(SDL_GetAppState() & SDL_APPACTIVE)
  {
    while(SDL_PollEvent(&Event))
      ProcessMessage(&Event);
  }
  else
  {
    SDL_WaitEvent(&Event);
    ProcessMessage(&Event);
  }

  return KeyBuffer.size() ? GetKey(false) : 0;
}

void globalwindowhandler::UpdateTick()
{
  Tick = SDL_GetTicks() / 40;
}

void globalwindowhandler::SetQuitMessageHandler(truth (*What)())
{
  QuitMessageHandler = What;
}

static void ProcessMessage(SDL_Event* Event)
{
  int KeyPressed;

  switch(Event->active.type)
  {
   case SDL_VIDEOEXPOSE:
    graphics::BlitDBToScreen();
    break;
   case SDL_QUIT:
    if(!QuitMessageHandler || QuitMessageHandler())
      exit(0);

    return;
   case SDL_KEYDOWN:
    switch(Event->key.keysym.sym)
    {
     case SDLK_RETURN:
     case SDLK_KP_ENTER:
      if(Event->key.keysym.mod & KMOD_ALT)
      {
	graphics::SwitchMode();
	return;
      }
      else
	KeyPressed = KEY_ENTER; //Event->key.keysym.unicode;

      break;
     case SDLK_DOWN:
     case SDLK_KP2:
      KeyPressed = KEY_DOWN + 0xE000;
      break;
     case SDLK_UP:
     case SDLK_KP8:
      KeyPressed = KEY_UP + 0xE000;
      break;
     case SDLK_RIGHT:
     case SDLK_KP6:
      KeyPressed = KEY_RIGHT + 0xE000;
      break;
     case SDLK_LEFT:
     case SDLK_KP4:
      KeyPressed = KEY_LEFT + 0xE000;
      break;
     case SDLK_HOME:
     case SDLK_KP7:
      KeyPressed = KEY_HOME + 0xE000;
      break;
     case SDLK_END:
     case SDLK_KP1:
      KeyPressed = KEY_END + 0xE000;
      break;
     case SDLK_PAGEUP:
     case SDLK_KP9:
      KeyPressed = KEY_PAGE_UP + 0xE000;
      break;
     case SDLK_KP3:
     case SDLK_PAGEDOWN:
      KeyPressed = KEY_PAGE_DOWN + 0xE000;
      break;
     case SDLK_KP5:
      KeyPressed = '.';
      break;
     case SDLK_SYSREQ:
     case SDLK_PRINT:
#ifdef WIN32
      DOUBLE_BUFFER->Save("Scrshot.bmp");
#else
      DOUBLE_BUFFER->Save(festring(getenv("HOME")) + "/Scrshot.bmp");
#endif
      return;
     case SDLK_e:
      if(Event->key.keysym.mod & KMOD_ALT
	 && (Event->key.keysym.mod & KMOD_LCTRL
	     || Event->key.keysym.mod & KMOD_RCTRL))
      {
	KeyPressed = '\177';
	break;
      }
      [[fallthrough]];
     default:
      KeyPressed = Event->key.keysym.unicode;

      if(!KeyPressed)
	return;
    }

    if(std::find(KeyBuffer.begin(), KeyBuffer.end(), KeyPressed)
       == KeyBuffer.end())
      KeyBuffer.push_back(KeyPressed);

    break;
  }
}
