/*
 * DLL to hook ovr_GetInputState and display a graphical view of controller
 * input state.
 */
#include "stdafx.h"

#include <stdio.h>
#include <Shlwapi.h>
#include <assert.h>
#include "MinHook.h"
#include "Include/OVR_CAPI.h"
#include "Include/CImg.h"

#define LOGFILE "controller_overlay_log.txt"
#if _WIN64
#define OVRMODNAME "LibOVRRT64_1.dll"
#else
#define OVRMODNAME "LibOVRRT32_1.dll"
#endif

static struct {
	int resx;
	int resy;
	int fontsize;
} config = {800, 200, 20};

using namespace cimg_library;
CImgDisplay disp;

void installhook_ovr_GetInputState(void);

/* Original function pointers */
ovrResult (*_ovr_GetInputState)(ovrSession session,
				ovrControllerType controllerType,
				ovrInputState *inputState) = NULL;
HMODULE(WINAPI *_LoadLibraryW)(LPCWSTR lpFileName);

/* Hooks */
static bool hook_LoadLibraryW_enabled;
HMODULE WINAPI hook_LoadLibraryW(LPCWSTR lpFileName)
{
	LPCWSTR name = PathFindFileNameW(lpFileName);
	LPCWSTR ext = PathFindExtensionW(name);
	size_t length = ext - name;

	/* Call actual version */
	HMODULE ret = _LoadLibraryW(lpFileName);

	/* If OVRRT is now loaded lets go hook it */
	if (wcsncmp(name, L"" OVRMODNAME, length) == 0)
		installhook_ovr_GetInputState();

	return ret;
}

static bool hook_ovr_GetInputState_enabled;
ovrResult hook_ovr_GetInputState(ovrSession session,
				 ovrControllerType controllerType,
				 ovrInputState *inputState)
{
	ovrResult r = _ovr_GetInputState(session, controllerType, inputState);

	if (controllerType == ovrControllerType_Touch) {
		static const unsigned char red[] = {255, 0, 0};
		static const unsigned char green[] = {0, 255, 0};
		static const unsigned char blue[] = {0, 0, 255};
		static const unsigned char white[] = {255, 255, 255};

		CImg<unsigned char> thumbsticks[ovrHand_Count];
		CImg<unsigned char> buttons[ovrHand_Count];

		const unsigned char *color;

		for (int i = 0; i < ovrHand_Count; i++) {

			/* Thumbstick */
			float tsr = 0.5 * min(config.resx, config.resy);
			float tspr = 0.1 * tsr;
			float tsresx = 2.5 * tsr;
			float tsresy = 2.5 * tsr;
			int bm = (i == ovrHand_Left) ? ovrButton_LThumb
						     : ovrButton_RThumb;
#if 0
			FILE *fp = fopen("render_" LOGFILE, "a");

			fprintf(fp,
			       "[%d] tsr: %f \t tspr %f \t tsresx %f \t "
			       "tsresy %f\n",
			       i, tsr, tspr, tsresx, tsresy);

			fclose(fp);
#endif
			color = (inputState->Buttons & bm) ? green : red;
			thumbsticks[i] =
				CImg<unsigned char>(tsresx, tsresy, 1, 3, 1);
			thumbsticks[i].draw_circle(0.5 * tsresx, 0.5 * tsresy,
						   tsr, blue, 1);
			thumbsticks[i].draw_circle(
				0.5 * tsresx
					+ tsr * inputState->Thumbstick[i].x,
				0.5 * tsresx
					+ tsr * -inputState->Thumbstick[i].y,
				tspr, color, 1);

			/* Buttons & triggers */
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

void installhook_ovr_GetInputState()
{
	int ret = true;
	int mhr;
	HMODULE h;
	FARPROC p;
	FILE *fp = nullptr;

	fp = fopen(LOGFILE, "a");

	h = GetModuleHandleA(OVRMODNAME);
	if (!h) {
		fprintf(fp, "Couldn't get " OVRMODNAME " handle\n");
		ret = false;
		goto done;
	}

	p = GetProcAddress(h, "ovr_GetInputState");
	if (!p) {
		fprintf(fp, "Couldn't find ovr_GetInputState");
		ret = false;
		goto done;
	}

	if (!_ovr_GetInputState
	    && (mhr = MH_CreateHook(
			p, hook_ovr_GetInputState,
			reinterpret_cast<LPVOID *>(&_ovr_GetInputState)))
		       != MH_OK) {
		fprintf(fp, "Couldn't create hook: %d\n", mhr);
		ret = false;
		goto done;
	}
	if (!hook_ovr_GetInputState_enabled
	    && (mhr = MH_EnableHook(p)) != MH_OK) {
		fprintf(fp, "MH_EnableHook failed\n");
		ret = false;
		goto done;
	} else
		hook_ovr_GetInputState_enabled = true;

done:
	if (fp)
		fclose(fp);
}

bool installhook_LoadLibraryW()
{
	if (MH_CreateHook(&LoadLibraryW, &hook_LoadLibraryW,
			  reinterpret_cast<LPVOID *>(&_LoadLibraryW))
	    != MH_OK)
		return false;

	if (MH_EnableHook(&LoadLibraryW) != MH_OK)
		return false;

	return true;
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call,
		      LPVOID lpReserved)
{
	FILE *fp = nullptr;
	HANDLE h;
	BOOL ret = TRUE;

	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
		fp = fopen(LOGFILE, "w");

		if (MH_Initialize() != MH_OK) {
			fprintf(fp, "MH_Initialize() failed\n");
			ret = false;
			goto done;
		}

		h = GetModuleHandleA(OVRMODNAME);

		/* If not loaded yet, hook LoadLibraryW */
		if (!h && !installhook_LoadLibraryW()) {
			fprintf(fp, "Couldn't hook LoadLibraryW");
			ret = false;
			goto done;
		} else {
			/* If loaded, hook ovr_GetInputState */
			installhook_ovr_GetInputState();
		}
	case DLL_THREAD_ATTACH:
		break;
	case DLL_PROCESS_DETACH:
		disp.close();
		disp.~CImgDisplay();
	case DLL_THREAD_DETACH:
		break;
	}

done:
	if (fp)
		fclose(fp);

	return ret;
}
