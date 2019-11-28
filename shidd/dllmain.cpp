// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"

#include "MinHook.h"
#include "Include/OVR_CAPI.h"
#include <stdio.h>
#include "Include\CImg.h"

ovrResult (*gis)(ovrSession session, ovrControllerType controllerType,
		 ovrInputState *inputState) = NULL;
bool enabled = false;

using namespace cimg_library;

CImgDisplay disp;

ovrResult mygis(ovrSession session, ovrControllerType controllerType,
		ovrInputState *inputState)
{
	ovrResult r = gis(session, controllerType, inputState);

	if (controllerType == ovrControllerType_Touch) {
		static int resx = 800;
		static int resy = 200;

		static const unsigned char red[] = {255, 0, 0};
		static const unsigned char green[] = {0, 255, 0};
		static const unsigned char blue[] = {0, 0, 255};
		static const int fontsize = 18;

		CImg<unsigned char> thumbsticks[2];
		CImg<unsigned char> buttons[2];
		CImg<unsigned char> triggers[2];
		CImg<unsigned char> composite(resx, resy);

		const unsigned char *color;

		for (int i = 0; i < 2; i++) {

			/* Thumbstick */
			float tsr = 0.5 * resy;
			float tspr = 0.1 * tsr;
			float tsresx = 2.5 * tsr;
			float tsresy = 2.5 * tsr;
			int bm = (i == ovrHand_Left) ? ovrButton_LThumb
						     : ovrButton_RThumb;
			color = (inputState->Buttons & bm) ? green : red;
			thumbsticks[i] =
				CImg<unsigned char>(tsresx, tsresy, 1, 3, 1);
			thumbsticks[i].draw_circle(tsresx / 2.0, tsresy / 2.0,
						   tsr, blue, 1);
			thumbsticks[i].draw_circle(
				tsresx / 2.0
					+ tsr * inputState->Thumbstick[i].x,
				tsresx / 2.0
					+ tsr * -inputState->Thumbstick[i].y,
				tspr, color, 1);

			/* Buttons */
			float bar = 0.25 * tsr;
			int topbutton, bottombutton;
			const char *topbuttonletter, *bottombuttonletter;
			if (i == ovrHand_Left) {
				topbutton = ovrButton_Y;
				bottombutton = ovrButton_X;
				topbuttonletter = "Y";
				bottombuttonletter = "X";
			} else {
				topbutton = ovrButton_B;
				bottombutton = ovrButton_A;
				topbuttonletter = "B";
				bottombuttonletter = "A";
			}
			buttons[i] = CImg<unsigned char>(2.5 * bar, 4.5 * bar,
							 1, 3, 1);
			color = inputState->Buttons & topbutton ? red : blue;
			buttons[i].draw_circle(bar, bar, bar, color, 1.0);
			buttons[i].draw_text(bar, bar, topbuttonletter, 0,
					     color, 1, fontsize);
			color = inputState->Buttons & bottombutton ? red : blue;
			buttons[i].draw_circle(bar, 3 * bar + 0.1 * bar, bar,
					       color, 1.0);
			buttons[i].draw_text(bar, 3 * bar + 0.1 * bar,
					     bottombuttonletter, 0, color, 1,
					     fontsize);
		}

		CImgList<unsigned char> list(thumbsticks[0], buttons[0],
					     buttons[1], thumbsticks[1]);

		disp.display(list);
	}

	return r;
}

#define LOGFILE "controller_overlay_log.txt"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call,
		      LPVOID lpReserved)
{
	int ret = true;
	int mhr;
	HMODULE h;
	FARPROC p;
	FILE *fp = nullptr;

	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
		fp = fopen(LOGFILE, "w");

		if (MH_Initialize() != MH_OK) {
			fprintf(fp, "MH_Initialize() failed\n");
			ret = false;
			goto done;
		}
	case DLL_THREAD_ATTACH:
		if (!fp)
			fp = fopen(LOGFILE, "a");

		h = GetModuleHandleA("LibOVRRT64_1.dll");
		if (!h) {
			fprintf(fp, "Couldn't get OVRRT handle\n");
			ret = false;
			goto done;
		}

		p = GetProcAddress(h, "ovr_GetInputState");
		if (!p) {
			fprintf(fp, "Couldn't find ovr_GetInputState");
			ret = false;
			goto done;
		}

		if (!gis
		    && (mhr = MH_CreateHook(p, mygis,
					    reinterpret_cast<LPVOID *>(&gis)))
			       != MH_OK) {
			fprintf(fp, "Couldn't create hook: %d\n", mhr);
			ret = false;
			goto done;
		}
		if (!enabled && MH_EnableHook(p) != MH_OK) {
			fprintf(fp, "MH_EnableHook failed\n");
			ret = false;
			goto done;
		}
		break;
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}

done:
	if (fp)
		fclose(fp);

	return ret;
}
