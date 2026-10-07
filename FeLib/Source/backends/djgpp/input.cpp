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

/* DJGPP/DOS input backend: keyboard polling via pc.h/keys.h. */

#include <ctime>

#include <pc.h>
#include <keys.h>

#include "whandler.h"
#include "graphics.h"
#include "bitmap.h"
#include "festring.h"

void globalwindowhandler::Init()
{
}

void globalwindowhandler::SetQuitMessageHandler(truth (*))
{
}

void globalwindowhandler::UpdateTick()
{
  Tick = uclock() * 25 / UCLOCKS_PER_SEC;
}

int globalwindowhandler::GetKey(truth EmptyBuffer)
{
  if(EmptyBuffer)
    while(kbhit())
      getkey();

  int Key = 0;

  while(!Key)
  {
    while(!kbhit())
      if(Controls && ControlLoopsEnabled)
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
      }

    Key = getkey();

    if(Key == K_Control_Print)
    {
      DOUBLE_BUFFER->Save("Scrshot.bmp");
      Key = 0;
    }
  }

  return Key;
}

int globalwindowhandler::ReadKey()
{
  return kbhit() ? getkey() : 0;
}
