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

/* SDL2 input backend: event pump, key decoding and timing.

   Character keys arrive as SDL_TEXTINPUT events (the SDL 1.2 'unicode'
   mechanism does not exist in SDL2); function/cursor/control keys arrive
   as SDL_KEYDOWN events. SDL 1.2's SDL_GetAppState()/SDL_APPACTIVE have no
   SDL2 counterpart, so the visibility/focus state is tracked ourselves from
   window events. */

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

static truth Active = true;

static void ProcessMessage(SDL_Event*);

void globalwindowhandler::Init()
{
  /* SDL_EnableUNICODE / SDL_EnableKeyRepeat do not exist in SDL2: the
     platform generates text input and key repeat on its own. */
  SDL_StartTextInput();
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
	if(Active
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

  if(Active)
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

  switch(Event->type)
  {
   case SDL_WINDOWEVENT:
    switch(Event->window.event)
    {
     case SDL_WINDOWEVENT_EXPOSED:
     case SDL_WINDOWEVENT_SIZE_CHANGED:
      graphics::BlitDBToScreen();
      break;
     case SDL_WINDOWEVENT_FOCUS_GAINED:
     case SDL_WINDOWEVENT_SHOWN:
     case SDL_WINDOWEVENT_RESTORED:
      Active = true;
      break;
     case SDL_WINDOWEVENT_FOCUS_LOST:
     case SDL_WINDOWEVENT_HIDDEN:
      Active = false;
      break;
    }

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
	KeyPressed = KEY_ENTER;

      break;
     case SDLK_DOWN:
     case SDLK_KP_2:
      KeyPressed = KEY_DOWN + 0xE000;
      break;
     case SDLK_UP:
     case SDLK_KP_8:
      KeyPressed = KEY_UP + 0xE000;
      break;
     case SDLK_RIGHT:
     case SDLK_KP_6:
      KeyPressed = KEY_RIGHT + 0xE000;
      break;
     case SDLK_LEFT:
     case SDLK_KP_4:
      KeyPressed = KEY_LEFT + 0xE000;
      break;
     case SDLK_HOME:
     case SDLK_KP_7:
      KeyPressed = KEY_HOME + 0xE000;
      break;
     case SDLK_END:
     case SDLK_KP_1:
      KeyPressed = KEY_END + 0xE000;
      break;
     case SDLK_PAGEUP:
     case SDLK_KP_9:
      KeyPressed = KEY_PAGE_UP + 0xE000;
      break;
     case SDLK_KP_3:
     case SDLK_PAGEDOWN:
      KeyPressed = KEY_PAGE_DOWN + 0xE000;
      break;
     case SDLK_KP_5:
      KeyPressed = '.';
      break;
     case SDLK_SYSREQ:
     case SDLK_PRINTSCREEN:
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

      /* Plain 'e' is delivered by SDL_TEXTINPUT. */
      return;
     case SDLK_BACKSPACE:
      KeyPressed = KEY_BACK_SPACE;
      break;
     case SDLK_TAB:
      KeyPressed = '\t';
      break;
     case SDLK_ESCAPE:
      KeyPressed = KEY_ESC;
      break;
     case SDLK_DELETE:
      KeyPressed = '\177';
      break;
     default:
      /* Printable characters arrive as SDL_TEXTINPUT; drop all other
	 keycodes (function keys etc. are not used by the game). */
      return;
    }

    if(std::find(KeyBuffer.begin(), KeyBuffer.end(), KeyPressed)
       == KeyBuffer.end())
      KeyBuffer.push_back(KeyPressed);

    break;
   case SDL_TEXTINPUT:
    /* The game only consumes ASCII (GetKey drops anything >= 0x81), and
       SDL2 delivers UTF-8 here. Ignore any text generated while a control
       key is held, which is not meaningful game input. */
    if(SDL_GetModState() & KMOD_CTRL)
      return;

    KeyPressed = static_cast<uchar>(Event->text.text[0]);

    if(KeyPressed >= 0x80)
      return;

    if(std::find(KeyBuffer.begin(), KeyBuffer.end(), KeyPressed)
       == KeyBuffer.end())
      KeyBuffer.push_back(KeyPressed);

    break;
  }
}