#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdint>

#define NEPMOD_INIT extern "C" __declspec(dllexport) void __cdecl NepMod_Init()

namespace NepSDK
{
	constexpr uintptr_t kPreferredImageBase = 0x00400000;

	inline void *Rebase(uintptr_t staticAddress)
	{
		uintptr_t rva = staticAddress - kPreferredImageBase;
		return (void *)((uintptr_t)GetModuleHandleA(nullptr) + rva);
	}

	inline bool PatchBytes(void *address, const unsigned char *expected,
		const unsigned char *replacement, size_t length)
	{
		if (memcmp(address, expected, length) != 0)
			return false;

		DWORD oldProtect;
		if (!VirtualProtect(address, length, PAGE_EXECUTE_READWRITE, &oldProtect))
			return false;

		memcpy(address, replacement, length);
		VirtualProtect(address, length, oldProtect, &oldProtect);
		FlushInstructionCache(GetCurrentProcess(), address, length);
		return true;
	}

	namespace detail
	{
		struct FindWindowCtx
		{
			DWORD pid;
			HWND result;
		};

		inline BOOL CALLBACK FindWindowProc(HWND hwnd, LPARAM lParam)
		{
			auto *ctx = (FindWindowCtx *)lParam;
			DWORD pid = 0;
			GetWindowThreadProcessId(hwnd, &pid);
			if (pid == ctx->pid && GetWindow(hwnd, GW_OWNER) == nullptr && IsWindowVisible(hwnd))
			{
				ctx->result = hwnd;
				return FALSE;
			}
			return TRUE;
		}
	}

	inline HWND FindGameWindow()
	{
		detail::FindWindowCtx ctx{ GetCurrentProcessId(), nullptr };
		EnumWindows(detail::FindWindowProc, (LPARAM)&ctx);
		return ctx.result;
	}

	namespace detail
	{
		inline const char *GameRootDir()
		{
			static char dir[MAX_PATH] = {};
			if (dir[0] == 0)
			{
				GetModuleFileNameA(nullptr, dir, MAX_PATH);
				char *slash = strrchr(dir, '\\');
				if (!slash) slash = strrchr(dir, '/');
				if (slash) *slash = 0;
			}
			return dir;
		}
	}

	inline void Log(const char *fmt, ...)
	{
		char buf[512];
		va_list args;
		va_start(args, fmt);
		vsnprintf(buf, sizeof(buf), fmt, args);
		va_end(args);

		OutputDebugStringA(buf);
		OutputDebugStringA("\n");

		char path[MAX_PATH];
		snprintf(path, sizeof(path), "%s\\NepSDK.log", detail::GameRootDir());
		if (FILE *f = fopen(path, "a"))
		{
			fputs(buf, f);
			fputc('\n', f);
			fclose(f);
		}
	}

	namespace detail
	{
		struct WaitForWindowArgs
		{
			void (*onFound)(HWND);
			DWORD pollIntervalMs;
			DWORD timeoutMs;
		};

		inline DWORD WINAPI WaitForWindowThread(LPVOID param)
		{
			auto *args = (WaitForWindowArgs *)param;

			DWORD waited = 0;
			HWND hwnd = nullptr;
			while (waited < args->timeoutMs)
			{
				hwnd = FindGameWindow();
				if (hwnd)
					break;
				Sleep(args->pollIntervalMs);
				waited += args->pollIntervalMs;
			}

			if (hwnd)
				args->onFound(hwnd);
			else
				Log("NepSDK: gave up waiting for the game window after %lu ms", args->timeoutMs);

			delete args;
			return 0;
		}
	}

	inline void WaitForGameWindowAsync(void (*onFound)(HWND), DWORD pollIntervalMs = 250, DWORD timeoutMs = 30000)
	{
		auto *args = new detail::WaitForWindowArgs{ onFound, pollIntervalMs, timeoutMs };
		HANDLE hThread = CreateThread(nullptr, 0, detail::WaitForWindowThread, args, 0, nullptr);
		if (hThread)
			CloseHandle(hThread);
	}

	namespace detail
	{
		struct WaitForPointerArgs
		{
			void **address;
			void (*onReady)(void *value);
			DWORD pollIntervalMs;
			DWORD timeoutMs;
		};

		inline DWORD WINAPI WaitForPointerThread(LPVOID param)
		{
			auto *args = (WaitForPointerArgs *)param;

			DWORD waited = 0;
			void *value = nullptr;
			while (waited < args->timeoutMs)
			{
				value = *args->address;
				if (value)
					break;
				Sleep(args->pollIntervalMs);
				waited += args->pollIntervalMs;
			}

			if (value)
				args->onReady(value);
			else
				Log("NepSDK: gave up waiting for a non-null pointer at %p after %lu ms", (void *)args->address, args->timeoutMs);

			delete args;
			return 0;
		}
	}

	inline void WaitForPointerAsync(void **address, void (*onReady)(void *value), DWORD pollIntervalMs = 100, DWORD timeoutMs = 30000)
	{
		auto *args = new detail::WaitForPointerArgs{ address, onReady, pollIntervalMs, timeoutMs };
		HANDLE hThread = CreateThread(nullptr, 0, detail::WaitForPointerThread, args, 0, nullptr);
		if (hThread)
			CloseHandle(hThread);
	}

	inline void *HookVTableMethod(void *comObject, int methodIndex, void *newMethod)
	{
		auto *vtable = *(uintptr_t **)comObject;
		uintptr_t *slot = &vtable[methodIndex];
		void *original = (void *)*slot;

		DWORD oldProtect;
		VirtualProtect(slot, sizeof(uintptr_t), PAGE_READWRITE, &oldProtect);
		*slot = (uintptr_t)newMethod;
		VirtualProtect(slot, sizeof(uintptr_t), oldProtect, &oldProtect);

		return original;
	}
}
