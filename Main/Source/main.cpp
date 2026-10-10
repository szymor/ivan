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

#include <iostream>
#include <cstdlib>

#ifdef __DJGPP__
#include <go32.h>
#include <sys/farptr.h>
#endif

#include "game.h"
#include "database.h"
#include "feio.h"
#include "igraph.h"
#include "iconf.h"
#include "whandler.h"
#include "hscore.h"
#include "graphics.h"
#include "script.h"
#include "message.h"
#include "proto.h"

/* The `--wildernesstest` diagnostic itself lives in wildernesstest.cpp; this
   file only dispatches to it. */

#ifdef WILDERNESS_TEST_HARNESS

/* Defined in wildernesstest.cpp, which holds the whole diagnostic. */
bool BeginWildernessIsolation(const char*);
int WildernessAbortTest(const char*);
int WildernessResumeCase(const char*, const char*);
int WildernessTest(int);

#endif

int Main(int argc, char **argv)
{
  if(argc > 1 && festring(argv[1]) == "--version")
  {
    std::cout << "Iter Vehemens ad Necem version " << IVAN_VERSION << std::endl;
    return 0;
  }

#ifdef WILDERNESS_TEST_HARNESS
  /* T5: validate the whole invocation before creating anything, so a bad one
     costs nothing and leaves no directory behind. The diagnostic takes over
     its environment before ivanconfig::Initialize() and everything after it,
     so no ordinary configuration or save file is even read on the way in. An
     environment that cannot be isolated fails closed. */
  bool WildernessMode = argc > 1 && festring(argv[1]) == "--wildernesstest";
  int WildernessSeeds = 10;

  if(WildernessMode)
  {
    if(argc > 2)
    {
      char* End = 0;
      long Value = strtol(argv[2], &End, 10);

      if(End == argv[2] || *End || Value < 1 || Value > 4096)
      {
	std::cout << "FAIL invalid seed count '" << argv[2]
		  << "'; expected an integer in 1..4096" << std::endl;
	std::cout << "WILDERNESS TEST FAILED: 1 failures" << std::endl;
	return 1;
      }

      WildernessSeeds = int(Value);
    }

    if(!BeginWildernessIsolation(argv[0]))
    {
      std::cout << "WILDERNESS TEST FAILED: 1 failures" << std::endl;
      return 1;
    }
  }
#endif

#ifdef __DJGPP__

  /* Saves numlock state and toggles it off */

  char ShiftByteState = _farpeekb(_dos_ds, 0x417);
  _farpokeb(_dos_ds, 0x417, 0);

#endif /* __DJGPP__ */

  femath::SetSeed(time(0));
  game::InitGlobalValueMap();
  scriptsystem::Initialize();
  databasesystem::Initialize();
  game::InitLuxTable();
  ivanconfig::Initialize();
  igraph::Init();
  game::CreateBusyAnimationCache();
  globalwindowhandler::SetQuitMessageHandler(game::HandleQuitMessage);
  msgsystem::Init();
  protosystem::Initialize();
  igraph::LoadMenu();

#ifdef WILDERNESS_TEST_HARNESS
  /* The children this run spawns inherit the already-isolated environment, so
     they are dispatched before the mode check and never isolate again. */
  if(argc > 1 && festring(argv[1]) == "--wildernessabort")
    return WildernessAbortTest(argc > 2 ? argv[2] : 0);

  if(argc > 3 && festring(argv[1]) == "--wildernessresume")
    return WildernessResumeCase(argv[2], argv[3]);

  if(WildernessMode)
    return WildernessTest(WildernessSeeds);
#endif

  for(;;)
  {
    int Select = iosystem::Menu(igraph::GetMenuGraphic(),
				v2(RES.X / 2, RES.Y / 2 - 20),
				CONST_S("\r"),
				CONST_S("Start Game\rContinue Game\r"
					"Configuration\rHighscores\r"
					"Quit\r"),
				LIGHT_GRAY,
				CONST_S("Released under the GNU\r"
					"General Public License\r"
					"More info: see COPYING\r"),
				CONST_S("IVAN v" IVAN_VERSION "\r"));

    switch(Select)
    {
     case 0:
      if(game::Init())
      {
	igraph::UnLoadMenu();

	game::Run();
	game::DeInit();
	igraph::LoadMenu();
      }

      break;
     case 1:
      {
	festring LoadName = iosystem::ContinueMenu(WHITE, LIGHT_GRAY, game::GetSaveDir());

	if(LoadName.GetSize())
	{
	  LoadName.Resize(LoadName.GetSize() - 4);

	  if(game::Init(LoadName))
	  {
	    igraph::UnLoadMenu();
	    game::Run();
	    game::DeInit();
	    igraph::LoadMenu();
	  }
	}

	break;
      }
     case 2:
      ivanconfig::Show();
      break;
     case 3:
      {
	highscore HScore;
	HScore.Draw();
	break;
      }
     case 4:

#ifdef __DJGPP__

      /* Loads numlock state */

      _farpokeb(_dos_ds, 0x417, ShiftByteState);

#endif

      return 0;
    }
  }
}
