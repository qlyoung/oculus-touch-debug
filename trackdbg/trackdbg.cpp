// trackdbg.cpp : This file contains the 'main' function. Program execution
// begins and ends there.
//

#include "pch.h"
#include <windows.h>

#include <iostream>

#include <Shlwapi.h>
#include <stdlib.h>

#include "inject.h"

FILE *g_LogFile;

bool InjectDLL(HANDLE hProcess, HANDLE hThread, const char *dllPath,
	       int dllPathLength);

int GetAbsolutePath(char *path, int length, const char *fileName)
{
	char cwd[MAX_PATH];
	GetModuleFileNameA(NULL, cwd, MAX_PATH);
	PathRemoveFileSpecA(cwd);
	return snprintf(path, length, "%s\\%s", cwd, fileName);
}

bool InjectDLL(HANDLE hProcess, HANDLE hThread, const char *dllPath,
	       int dllPathLength)
{
	LOG("Injecting DLL: %s\n", dllPath);

	HMODULE hModule = GetModuleHandle(L"kernel32.dll");
	LPVOID loadLibraryAddr =
		(LPVOID)GetProcAddress(hModule, "LoadLibraryA");
	if (loadLibraryAddr == NULL) {
		LOG("Unable to locate LoadLibraryA\n");
		return false;
	}
	LOG("LoadLibrary found at address: 0x%x\n", loadLibraryAddr);

	LPVOID loadLibraryArg = (LPVOID)VirtualAllocEx(
		hProcess, NULL, dllPathLength, MEM_RESERVE | MEM_COMMIT,
		PAGE_READWRITE);
	if (loadLibraryArg == NULL) {
		LOG("Could not allocate memory in process\n");
		return false;
	}

	int n = WriteProcessMemory(hProcess, loadLibraryArg, dllPath,
				   dllPathLength, NULL);
	if (n == 0) {
		LOG("Could not write to process's address space\n");
		return false;
	}

	LOG("Wrote proc memory\n");

	if (hThread != INVALID_HANDLE_VALUE) {
		if (QueueUserAPC((PAPCFUNC)loadLibraryAddr, hThread,
				 (ULONG_PTR)loadLibraryArg)) {
			LOG("Queued user APC succesfully, Revive will be "
			    "loaded when the thread "
			    "enters an alertable state\n");
			return true;
		}
		LOG("Failed to queue user APC, falling back to a remote "
		    "thread\n");
	}

	LOG("Spinning up proc\n");

	hThread = CreateRemoteThread(hProcess, NULL, 0,
				     (LPTHREAD_START_ROUTINE)loadLibraryAddr,
				     loadLibraryArg, NULL, NULL);
	if (hThread == INVALID_HANDLE_VALUE) {
		LOG("Failed to create remote thread in process\n");
		return false;
	}

	DWORD waitReturnValue = WaitForSingleObject(hThread, INFINITE);
	if (waitReturnValue != WAIT_OBJECT_0) {
		LOG("Failed to wait for LoadLibrary to exit\n");
		return false;
	}

	DWORD loadLibraryReturnValue;
	GetExitCodeThread(hThread, &loadLibraryReturnValue);
	if (loadLibraryReturnValue == NULL) {
		LOG("LoadLibrary failed to return module handle\n");
		return false;
	}

	return true;
}

bool InjectShidd(HANDLE hProcess, HANDLE hThread)
{

	char dllPath[MAX_PATH];
	strcpy(dllPath,
	       "C:\\Program "
	       "Files\\Oculus\\Support\\oculus-runtime\\LibOVRRT64_1.dll");
	LOG("DLL Path %s\n", dllPath);
	InjectDLL(hProcess, hThread, dllPath, MAX_PATH);

	GetAbsolutePath(dllPath, MAX_PATH, "shidd.dll");
	LOG("DLL Path %s\n", dllPath);
	return InjectDLL(hProcess, hThread, dllPath, MAX_PATH);
}

unsigned int CreateProcessAndInject(wchar_t *programPath, bool apc)
{
	LOG("Creating process: %ls\n", programPath);

	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	SECURITY_ATTRIBUTES sa;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));
	ZeroMemory(&sa, sizeof(sa));
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	wchar_t workingDir[MAX_PATH];
	wcsncpy(workingDir, programPath, MAX_PATH);

	// Remove extension
	wchar_t *ext = wcsstr(workingDir, L".exe");
	if (ext)
		*ext = L'\0';

	// Remove filename
	wchar_t *file = wcsrchr(workingDir, L'\\');
	if (file)
		*file = L'\0';

	// Create the injectee, specify the working directory if a path was
	// found
	if (!CreateProcess(NULL, programPath, &sa, NULL, FALSE,
			   CREATE_SUSPENDED, NULL,
			   (file && ext) ? workingDir : NULL, &si, &pi)) {
		LOG("Failed to create process\n");
		return pi.dwProcessId;
	}

#if _WIN64
	BOOL is32Bit = FALSE;
	if (!IsWow64Process(pi.hProcess, &is32Bit)) {
		LOG("Failed to query bit depth\n");
		return pi.dwProcessId;
	}
	if (is32Bit) {
		LOG("Delegating 32-bit process (%d) to x86 injector\n",
		    pi.dwProcessId);

		PROCESS_INFORMATION injector;
		ZeroMemory(&injector, sizeof(injector));
		wchar_t commandLine[MAX_PATH];
		GetModuleFileNameW(NULL, commandLine, MAX_PATH);
		wchar_t *substr = wcswcs(commandLine, L"x64");
		if (substr) {
			// Replace with x86
			substr[1] = '8';
			substr[2] = '6';
		}
		swprintf(commandLine, MAX_PATH, L"%s /handle %d", commandLine,
			 pi.hProcess);
		if (!CreateProcess(NULL, commandLine, NULL, NULL, TRUE, NULL,
				   NULL, NULL, &si, &injector)) {
			LOG("Failed to create injector\n");
			return -1;
		}

		DWORD waitReturnValue =
			WaitForSingleObject(injector.hThread, INFINITE);
		if (waitReturnValue != WAIT_OBJECT_0) {
			LOG("Failed to wait for injector to exit\n");
			return false;
		}
		ResumeThread(pi.hThread);
		return pi.dwProcessId;
	}
#endif
	HANDLE hThread = apc ? pi.hThread : INVALID_HANDLE_VALUE;

	InjectShidd(pi.hProcess, hThread);

	LOG("Injected dlls successfully\n");
	ResumeThread(pi.hThread);
	return pi.dwProcessId;
}

int OpenProcessAndInject(wchar_t *processId)
{
	LOG("Injecting process handle: %ls\n", processId);

	HANDLE hProcess = (HANDLE)wcstol(processId, nullptr, 0);
	if (hProcess == NULL) {
		LOG("Failed to get process handle\n");
		return -1;
	}
	LOG("Injected dlls succesfully\n");
	return 0;
}

int main()
{
	wchar_t nut[] =
		L"C:\\Program "
		L"Files\\Oculus\\Software\\Software\\ready-at-dawn-echo-"
		L"arena\\bin\\win7\\echovr.exe";
	uint32_t processId = CreateProcessAndInject(nut, false);
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started:
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add
//   Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project
//   and select the .sln file
