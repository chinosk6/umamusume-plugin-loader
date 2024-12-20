#include <stdinclude.hpp>
#include <unordered_set>
#include <ranges>
#include <set>
#include <Psapi.h>
#include <boost/regex.hpp>
#include <utils.hpp>

using namespace std;

std::unordered_map<std::string, std::string> g_il2cppSymbols{};

namespace
{
	void path_game_assembly();
	Il2CppString* (*environment_get_stacktrace)();

	bool mh_inited = false;

	const std::unordered_set<std::wstring> allowedEnds{ L"baselib.dll", L"GameAssembly.dll", L"umamusume.exe", 
		L"umamusume.exe._", L"UnityCrashHandler64.exe", L"UnityPlayer.dll", L".", L"..", L"umamusume_Data", L"UmamusumeUninstaller.exe"};
	std::unordered_set<HANDLE> needPatchHanle{};

	void* FindNextFileW_orig;
	BOOL FindNextFileW_hook(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData) {
		const auto findNextFileW_O = reinterpret_cast<decltype(FindNextFileW_hook)*>(FindNextFileW_orig);

		auto ret = findNextFileW_O(hFindFile, lpFindFileData);
		if (!needPatchHanle.contains(hFindFile)) {
			return ret;
		}

		if (ret) {
			//wprintf(L"FindNextFileW-catch: %ls at %p\n", lpFindFileData->cFileName, hFindFile);

			std::wstring currentName(lpFindFileData->cFileName);
			while (!allowedEnds.contains(currentName)) {
				if (!findNextFileW_O(hFindFile, lpFindFileData)) {
					needPatchHanle.erase(hFindFile);
					//printf("end\n");
					return FALSE;
				}
				currentName = std::wstring(lpFindFileData->cFileName);
				//wprintf(L"JumpNext: %ls\n", currentName.c_str());
			}
			needPatchHanle.emplace(hFindFile);
		}
		else {
			needPatchHanle.erase(hFindFile);
		}
		return ret;
	}

	void* FindFirstFileExW_orig;
	HANDLE FindFirstFileExW_hook(LPCWSTR lpFileName, FINDEX_INFO_LEVELS fInfoLevelId, LPWIN32_FIND_DATAW lpFindFileData,
		FINDEX_SEARCH_OPS fSearchOp, LPVOID lpSearchFilter, DWORD dwAdditionalFlags) {

		const auto currPath = std::filesystem::current_path();
		const auto targetPath = currPath / "*.*";

		auto handle = reinterpret_cast<decltype(FindFirstFileExW_hook)*>(FindFirstFileExW_orig)(lpFileName, fInfoLevelId, lpFindFileData,
			fSearchOp, lpSearchFilter, dwAdditionalFlags);
		if (std::wstring(lpFileName) != std::wstring(targetPath.c_str())) {
			return handle;
		}
		
		//wprintf(L"FindFirstFileExW_hook-catch: %ls\n", lpFileName);

		const auto findNextFileW_O = reinterpret_cast<decltype(FindNextFileW_hook)*>(FindNextFileW_orig);

		if (handle == INVALID_HANDLE_VALUE) {
			// printf("INVALID_HANDLE_VALUE\n");
			return handle;
		}

		std::wstring currentName(lpFindFileData->cFileName);
		//wprintf(L"FindFirstFileExW_hook: file: %ls\n", currentName.c_str());

		while (!allowedEnds.contains(currentName)) {
			if (!findNextFileW_O(handle, lpFindFileData)) return INVALID_HANDLE_VALUE;
			currentName = std::wstring(lpFindFileData->cFileName);
		}

		needPatchHanle.emplace(handle);
		return handle;
	}

	void* name_check_orig;
	unsigned __int8 name_check_hook(char* v) {
		auto ret = reinterpret_cast<unsigned __int8 (*)(char*)>(name_check_orig)(v);
		printf("name_check_hook: %s - %d\n", v, ret);
		// return true;
		return ret;
	}

	void* InitParameters_orig = nullptr;
	__int64 InitParameters_hook(unsigned int a1, unsigned __int8(*a2)(char*)) {
		// return 0;
		return 1;

		if (!name_check_orig) {
			MH_CreateHook(a2, name_check_hook, &name_check_orig);
			MH_EnableHook(a2);
		}

		auto ret = reinterpret_cast<decltype(InitParameters_hook)*>(InitParameters_orig)(a1, a2);
		return ret;

		std::vector<std::string> tests = {"version.dll", "winhttp.dll", "dxgi.dll", "cysb.dll"};
		for (auto& i : tests) {
			char* pa1 = const_cast<char*>(i.c_str());
			auto v = a2(pa1);
			printf("a2: %s - %d\n", i.c_str(), v);
		}
		if (environment_get_stacktrace) {
			wprintf(L"%ls\n\n", environment_get_stacktrace()->start_char);
		}

		printf("Native - InitParameters: a1: %u, a2 at %p, ret: %lld\n", a1, a2, ret);
		return ret;
	}

	void* GetFunctionPointerForDelegateInternal_orig;
	void* GetFunctionPointerForDelegateInternal_hook(void* a1) {
		auto delegate_klass = static_cast<Il2CppClassHead*>(il2cpp_symbols::get_class_from_instance(a1));
		auto get_Method = il2cpp_class_get_method_from_name(delegate_klass, "get_Method", 0);
		auto get_Method_invoke = reinterpret_cast<MethodInfo * (*)(void*)>(get_Method->methodPointer);

		printf("GetFunctionPointerForDelegateInternal: %p, %s\n", a1, get_Method_invoke(a1)->name);
		return reinterpret_cast<decltype(GetFunctionPointerForDelegateInternal_hook)*>(GetFunctionPointerForDelegateInternal_orig)(a1);
	}

	std::string GetIl2cppRealName(const std::string& requestName) {
		if (auto it = g_il2cppSymbols.find(requestName); it != g_il2cppSymbols.end()) {
			return it->second;
		}
		return requestName;
	}

	void* GetProcAddress_orig;
	FARPROC GetProcAddress_hook(HMODULE hModule, const char* lpProcName) {
		TCHAR szPath[MAX_PATH] = { 0 };
		if (GetModuleFileName(hModule, szPath, MAX_PATH) != 0)
		{
			const std::string moduleName(szPath);
			//printf("GetProcAddress - module: %s, proc: %s\n", moduleName.c_str(), lpProcName);

			if (moduleName.ends_with("GameAssembly.dll")) {
				// printf("GetProcAddress - module: %s, proc: %s\n", moduleName.c_str(), lpProcName);
				const auto newName = GetIl2cppRealName(lpProcName);
				auto ret = reinterpret_cast<decltype(GetProcAddress_hook)*>(GetProcAddress_orig)(hModule, newName.c_str());
				if (ret) {
					return ret;
				}
			}
		}

		return reinterpret_cast<decltype(GetProcAddress_hook)*>(GetProcAddress_orig)(hModule, lpProcName);
	}


	void patchBeforeCRI();

	bool patched = false;
	void patchOnInit() {
		// return;
		if (patched) return;
		patched = true;
		// printf("patchOnce\n");

		MH_CreateHook(GetProcAddress, GetProcAddress_hook, &GetProcAddress_orig);
		MH_EnableHook(GetProcAddress);

		MH_CreateHook(FindFirstFileExW, FindFirstFileExW_hook, &FindFirstFileExW_orig);
		MH_EnableHook(FindFirstFileExW);

		MH_CreateHook(FindNextFileW, FindNextFileW_hook, &FindNextFileW_orig);
		MH_EnableHook(FindNextFileW);

	}

	void* load_library_w_orig = nullptr;
	HMODULE __stdcall load_library_w_hook(const wchar_t* path)
	{
		// printf("load_library: %ls\n", path);
		// if (path == L"ole32.dll"sv)
		if (path == L"dxgi.dll"sv)
		// if (path == L"libnative.dll"sv)
		// if (path == L"cri_ware_unity.dll"sv)
		{
			// patchBeforeCRI();
			return reinterpret_cast<decltype(load_library_w_hook)*>(load_library_w_orig)(path);
		}

		if (path == L"libnative.dll"sv) {
			auto handle = reinterpret_cast<decltype(load_library_w_hook)*>(load_library_w_orig)(path);

			auto initParam_addr = GetProcAddress(handle, "InitParameters");
			if (initParam_addr) {
				if (!InitParameters_orig) {
					MH_CreateHook(initParam_addr, InitParameters_hook, &InitParameters_orig);
					MH_EnableHook(initParam_addr);
				}
			}

			return handle;
		}
		
		// GameAssembly.dll code must be loaded and decrypted while loading criware library
		if (path == L"cri_ware_unity.dll"sv)
		{
			MH_DisableHook(LoadLibraryW);
			MH_RemoveHook(LoadLibraryW);

			path_game_assembly();

			// use original function beacuse we have unhooked that
			return LoadLibraryW(path);
		}

		return reinterpret_cast<decltype(LoadLibraryW)*>(load_library_w_orig)(path);
	}

	void* InternalGetFileDirectoryNames_orig;
	Array<Il2CppString*>* InternalGetFileDirectoryNames_hook(Il2CppString* path, Il2CppString* userPathOriginal, Il2CppString* searchPattern, bool includeFiles, bool includeDirs, int32_t searchOption, bool checkHost) {
		wprintf(L"InternalGetFileDirectoryNames: %ls, %ls, %ls\n", path->start_char, userPathOriginal->start_char, searchPattern->start_char);

		//auto ret = reinterpret_cast<decltype(InternalGetFileDirectoryNames_hook)*>(InternalGetFileDirectoryNames_orig)(
		//	path, userPathOriginal, searchPattern, includeFiles, includeDirs, searchOption, checkHost
		//	);
		auto ret = reinterpret_cast<decltype(InternalGetFileDirectoryNames_hook)*>(InternalGetFileDirectoryNames_orig)(
			path, userPathOriginal, 
			searchPattern,
			includeFiles, includeDirs, searchOption, checkHost
			);
		/*
		auto printVec = ret->ToVector();
		for (const auto i : printVec) {
			wprintf(L"printVec: %ls\n", i->start_char);
		}
		return ret;*/


		static auto listKlass = il2cpp_symbols::get_system_class_from_reflection_type_str(
			"System.Collections.Generic.List`1[System.String]"
		);
		static auto listKlass_ctor_mtd = il2cpp_class_get_method_from_name(listKlass, ".ctor", 0);
		static auto listKlass_ctor = reinterpret_cast<void (*)(void*, void*)>(
			listKlass_ctor_mtd->methodPointer
			);

		printf("exec_end, listKlass at %p (%s)\n", listKlass, static_cast<Il2CppClassHead*>(listKlass)->name);


		auto vec = ret->ToVector();

		static std::vector<std::wstring> allowedEnds{L"baselib.dll", L"GameAssembly.dll", L"umamusume.exe", L"umamusume.exe._", L"UnityCrashHandler64.exe",
			L"UnityPlayer.dll", L"UmamusumeUninstaller.exe"};
		std::vector<std::wstring> leftItems{};

		for (const auto& i : vec) {
			const std::wstring fileName(i->start_char);
			for (const auto& allowed : allowedEnds) {
				if (fileName.ends_with(allowed)) {
					leftItems.push_back(fileName);
					// wprintf(L"pushed: %ls\n", fileName.c_str());
					break;
				}
			}
		}

		auto newList = il2cpp_object_new(listKlass);
		listKlass_ctor(newList, listKlass_ctor_mtd);
		CSListEditor<Il2CppString*> editor(newList, listKlass);
		for (const auto& i : leftItems) {
			editor.Add(il2cpp_symbols::NewWStr(i));
		}
		auto newArray = editor.ToArray();

		auto newVet = newArray->ToVector();
		for (const auto& i : newVet) {
			wprintf(L"newVet: %ls\n", i->start_char);
		}

		return newArray;
	}


#pragma region HOOK_MACRO
#define ADD_HOOK(_name_, _fmt_) \
	auto _name_##_offset = reinterpret_cast<void*>(_name_##_addr); \
	\
	printf(_fmt_, _name_##_offset); \
 	\
	MH_CreateHook(_name_##_offset, _name_##_hook, &_name_##_orig); \
	MH_EnableHook(_name_##_offset); 
#pragma endregion

	bool fPatched = false;
	void patchBeforeCRI() {
		return;  // 用不上这一坨

		if (fPatched) return;
		fPatched = true;
		
		printf("Start patching path...\n");
		auto il2cpp_module = GetModuleHandle("GameAssembly.dll");
		il2cpp_symbols::init(il2cpp_module);

		auto InternalGetFileDirectoryNames_addr = il2cpp_symbols::get_method_pointer(
			"mscorlib.dll", "System.IO",
			"Directory", "InternalGetFileDirectoryNames", 7
		);

		auto GetFunctionPointerForDelegateInternal_addr = il2cpp_symbols::get_method_pointer(
			"mscorlib.dll", "System.Runtime.InteropServices",
			"Marshal", "GetFunctionPointerForDelegateInternal", 1
		) - 0x2BB4DD0;

		environment_get_stacktrace = reinterpret_cast<decltype(environment_get_stacktrace)>(il2cpp_symbols::get_method_pointer("mscorlib.dll", "System", "Environment", "get_StackTrace", 0));

		ADD_HOOK(InternalGetFileDirectoryNames, "InternalGetFileDirectoryNames at %p\n");
		ADD_HOOK(GetFunctionPointerForDelegateInternal, "GetFunctionPointerForDelegateInternal at %p\n");
	}

	void path_game_assembly()
	{
		if (!mh_inited)
			return;

		printf("Trying to patch GameAssembly.dll...\n");

		auto il2cpp_module = GetModuleHandle("GameAssembly.dll");

		printf("il2cpp_module at %p\n", il2cpp_module);

		g_il2cppSymbols = PluginLoader::Requests::GetIl2cppSymbolMapWithCache(il2cpp_module);
		if (g_il2cppSymbols.empty()) {
			return;
		}

		// load il2cpp exported functions
		il2cpp_symbols::init(il2cpp_module);
		// 在这后面 ADD_HOOK

		printf("il2cpp init end\n");
		// return;

		environment_get_stacktrace = reinterpret_cast<decltype(environment_get_stacktrace)>(il2cpp_symbols::get_method_pointer("mscorlib.dll", "System", "Environment", "get_StackTrace", 0));

		wprintf(L"Start Loading Plugins.\n");
		PluginLoader::CheckAndLoadDll();
	}
}


bool init_hook()
{
	if (mh_inited)
		return false;

	if (MH_Initialize() != MH_OK)
		return false;

	mh_inited = true;

	patchOnInit();

	auto cri_ware_handle = GetModuleHandleW(L"cri_ware_unity.dll");
	if (cri_ware_handle) {
		path_game_assembly();
	}
	else {
		auto stat1 = MH_CreateHook(LoadLibraryW, load_library_w_hook, &load_library_w_orig);
		auto stat2 = MH_EnableHook(LoadLibraryW);
		// printf("LoadLibraryW: %s, %s\n", MH_StatusToString(stat1), MH_StatusToString(stat2));
	}

	return true;
}


void uninit_hook()
{
	if (!mh_inited)
		return;

	MH_DisableHook(MH_ALL_HOOKS);
	MH_Uninitialize();
}
