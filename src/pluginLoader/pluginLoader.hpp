#pragma once
#include "stdinclude.hpp"


namespace PluginLoader {
	bool loadDll(const std::wstring& dllName);
	bool loadDll(const std::string& dllName);
	bool freeDll(const std::wstring& dllName);
	std::unordered_map<std::wstring, HMODULE> getLoadedDll();
	void CheckAndLoadDll();

	namespace Requests {
		std::unordered_map<std::string, std::string> GetIl2cppSymbolMapWithCache(HMODULE il2cpp_module);
	}
}
