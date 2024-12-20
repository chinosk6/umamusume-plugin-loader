#include "mode_def.hpp"
#include <stdinclude.hpp>

#include <minizip/unzip.h>
#include <TlHelp32.h>

#include <unordered_set>
#include <charconv>
#include <cassert>
#include <format>
#include <cpprest/uri.h>
#include <cpprest/http_listener.h>
#include <ranges>
#include <windows.h>

extern bool init_hook();
extern void uninit_hook();
extern void start_console();

const auto CONSOLE_TITLE = L"Umamusume Plugin DLL Loader";
const std::string ConfigJson = "upl-config.json";

bool g_has_config_file = false;
bool g_enable_console = true;
bool g_confirm_every_times = false;
std::vector<std::string> g_pluginPath{};

namespace
{
	void create_debug_console()
	{
		AllocConsole();

		// open stdout stream
		auto _ = freopen("CONOUT$", "w+t", stdout);
		_ = freopen("CONOUT$", "w", stderr);
		_ = freopen("CONIN$", "r", stdin);

		SetConsoleTitleW(CONSOLE_TITLE);

		// set this to avoid turn japanese texts into question mark
		SetConsoleOutputCP(65001);
		std::locale::global(std::locale(""));

		wprintf(L"%ls Loaded! - By chinosk\n", CONSOLE_TITLE);
	}
}

namespace
{
	std::vector<std::string> read_config()
	{
		std::vector<std::string> dicts{};
#ifdef TLG_ONLY
		g_enable_console = false;
		g_has_config_file = true;
		g_confirm_every_times = false;
#else
		std::ifstream config_stream{ ConfigJson };
		
		if (!config_stream.is_open())
			return dicts;

		rapidjson::IStreamWrapper wrapper{ config_stream };
		rapidjson::Document document;

		document.ParseStream(wrapper);

		if (!document.HasParseError())
		{
			g_has_config_file = true;
			if (document.HasMember("enableConsole")) {
				g_enable_console = document["enableConsole"].GetBool();
			}
			if (document.HasMember("confirmEveryTimes")) {
				g_confirm_every_times = document["confirmEveryTimes"].GetBool();
		}
			if (document.HasMember("dllList")) {
				const auto& lst = document["dllList"].GetArray();
				g_pluginPath.clear();
				for (const auto& i : lst) {
					g_pluginPath.push_back(i.GetString());
				}
			}
			
		}
		config_stream.close();
#endif
		return dicts;
	}
}

LONG WINAPI AddressExceptionHandler(EXCEPTION_POINTERS* ExceptionInfo) {
	// Check if the exception is an access violation
	if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
		// Check if the violation address is above 0x7FFFFFFFFFFF
		if (ExceptionInfo->ExceptionRecord->ExceptionAddress > (PVOID)0x7FFFFFFFFFFF) {
			// Return EXCEPTION_CONTINUE_EXECUTION to ignore the exception and continue
			return EXCEPTION_CONTINUE_EXECUTION;
		}
	}
	// For other exceptions or addresses, use the default exception handler
	return EXCEPTION_CONTINUE_SEARCH;
}

// https://github.com/Kimjio/umamusume-localify
void* UnityMain_orig = nullptr;
extern "C" __declspec(dllexport) int __stdcall UnityMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd) {
	std::wstring module_name;
	module_name.resize(MAX_PATH);
	module_name.resize(GetModuleFileNameW(GetModuleHandleW(L"UnityPlayer.dll"), module_name.data(), MAX_PATH));

	std::filesystem::path module_path(module_name);

	std::wstring szISOLang;
	szISOLang.resize(5);
	szISOLang.resize(static_cast<size_t>(GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, szISOLang.data(), szISOLang.size()) - 1));

	if (module_path.parent_path() == std::filesystem::current_path()) {
		MessageBoxW(nullptr, L"Don't overwrite the existing UnityPlayer.dll.", L"Error", MB_ICONERROR);
		return 1;
	}

	try {
		std::filesystem::copy_file(std::filesystem::current_path().append(L"UnityPlayer.dll"), L"umamusume.exe.local\\UnityPlayer.orig.dll", std::filesystem::copy_options::update_existing);
	}
	catch (std::exception& e) {}

	auto unity = LoadLibraryW(L"UnityPlayer.orig.dll");

	UnityMain_orig = GetProcAddress(unity, "UnityMain");

	return reinterpret_cast<decltype(UnityMain)*>(UnityMain_orig)(hInstance, hPrevInstance, lpCmdLine, nShowCmd);
}


int __stdcall DllMain(HINSTANCE dllModule, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		AddVectoredExceptionHandler(1, AddressExceptionHandler);
		// the DMM Launcher set start path to system32 wtf????
		std::string module_name;
		module_name.resize(MAX_PATH);
		module_name.resize(GetModuleFileName(nullptr, module_name.data(), MAX_PATH));

		std::filesystem::path module_path(module_name);

		// check name
		if (module_path.filename() != "umamusume.exe")
			return 1;

		std::filesystem::current_path(
			module_path.parent_path()
		);


		read_config();

		if (g_enable_console)
			create_debug_console();

		std::thread init_thread([] {

			if (g_enable_console)
			{
				start_console();
			}

			init_hook();

			std::mutex mutex;
			std::condition_variable cond;
			std::atomic<bool> hookIsReady(false);

			// 依赖检查游戏版本的指针加载，因此在 hook 完成后再加载翻译数据
			std::unique_lock lock(mutex);
			cond.wait(lock, [&] {
				return hookIsReady.load(std::memory_order_acquire);
				});
			if (g_enable_console)
			{
				auto _ = freopen("CONOUT$", "w+t", stdout);
				_ = freopen("CONOUT$", "w", stderr);
				_ = freopen("CONIN$", "r", stdin);
			}

			});
		init_thread.detach();
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
		RemoveVectoredExceptionHandler(AddressExceptionHandler);
		uninit_hook();
	}
	return 1;
}
