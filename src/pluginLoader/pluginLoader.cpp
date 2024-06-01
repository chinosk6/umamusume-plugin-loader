#include "../mode_def.hpp"
#include "stdinclude.hpp"
#include <cpprest/asyncrt_utils.h>
#include <Psapi.h>


namespace PluginLoader {
	namespace {
		std::unordered_map<std::wstring, HMODULE> loadedDll{};
	}

	bool loadDll(const std::wstring& dllName) {
		if (loadedDll.contains(dllName)) return false;

		printf("Loading DLL: %ls ...\n", dllName.c_str());
		const auto dllPtr = LoadLibraryW(dllName.c_str());
		const auto isSuccess = dllPtr != NULL;
		if (isSuccess) loadedDll.emplace(dllName, dllPtr);
		printf("Load DLL: %ls %s.\n", dllName.c_str(), isSuccess ? "success" : "failed");
		return isSuccess;
	}

	bool loadDll(const std::string& dllName) {
		return loadDll(utility::conversions::utf8_to_utf16(dllName));
	}

	bool freeDll(const std::wstring& dllName) {
		if (const auto iter = loadedDll.find(dllName); iter != loadedDll.end()) {
			const auto dllPtr = iter->second;
			FreeLibraryAndExitThread(dllPtr, 0);
			loadedDll.erase(iter);
			return true;
		}
		return false;
	}

	std::unordered_map<std::wstring, HMODULE> getLoadedDll() {
		return loadedDll;
	}

    std::vector<std::string> SelectFiles() {
        OPENFILENAMEW ofn;       // common dialog box structure
        wchar_t szFile[260];    // buffer for file name
        std::vector<std::string> selectedFiles{};

        // Initialize OPENFILENAME
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = NULL;
        ofn.lpstrFile = szFile;
        ofn.lpstrFile[0] = '\0';
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = L"DLL Files\0*.dll\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrFileTitle = NULL;
        ofn.nMaxFileTitle = 0;
        ofn.lpstrInitialDir = NULL;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

        // Display the Open dialog box.
        if (GetOpenFileNameW(&ofn) == TRUE) {
            // If the user selected multiple files, the buffer contains the directory followed by the file names
            wchar_t* p = ofn.lpstrFile;
            std::wstring directory(p);
            p += directory.length() + 1;
            if (*p) { // Multiple files
                while (*p) {
                    std::wstring fileName(p);
                    selectedFiles.push_back(utility::conversions::to_utf8string(directory + L"\\" + fileName));
                    p += fileName.length() + 1;
                }
            }
            else { // Single file
                selectedFiles.push_back(utility::conversions::to_utf8string(directory));
            }
        }
        return selectedFiles;
    }

    bool FindIdentifierInModule(HMODULE hModule, const char* identifier) {
        MODULEINFO moduleInfo;
        GetModuleInformation(GetCurrentProcess(), hModule, &moduleInfo, sizeof(moduleInfo));

        const char* moduleBase = (const char*)moduleInfo.lpBaseOfDll;
        size_t moduleSize = moduleInfo.SizeOfImage;
        size_t identifierLen = strlen(identifier);

        for (size_t i = 0; i < moduleSize - identifierLen; ++i) {
            if (memcmp(moduleBase + i, identifier, identifierLen) == 0) {
                return true;
            }
        }

        return false;
}

	void CheckAndLoadDll() {
#ifndef TLG_ONLY
        if (g_confirm_every_times || !g_has_config_file) {
            const auto selectedFiles = SelectFiles();
            g_pluginPath.insert(g_pluginPath.end(), selectedFiles.begin(), selectedFiles.end());
        }
        for (const auto& i : g_pluginPath) {
            loadDll(i);
        }
        printf("%llu plugins loaded.\n", g_pluginPath.size());
        LoadLibraryW(L"cri_ware_unity.dll");
#else
        auto tlgModule = LoadLibraryW(L"tlg.dll");
        if (!tlgModule) {
            TerminateProcess(GetCurrentProcess(), 0);
        }
        if (!FindIdentifierInModule(tlgModule, "foramghl97")) {
            TerminateProcess(GetCurrentProcess(), 0);
        }
#endif
	}

}
