// Define GUIDs in this TU — all other TUs get extern declarations
#include <initguid.h>

#include "app.h"

#ifndef NDEBUG
int WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR cmdline, int cmdshow)
#else
void WinMainCRTStartup()
#endif
{
	AppState App = { 0 };

	if (!App_Init(&App))
	{
		ExitProcess(0);
	}

	int Result = App_Run(&App);

	ExitProcess(Result);
}
