#include "../mode_def.hpp"
#include "stdinclude.hpp"
#include <Psapi.h>
#include <cpprest/asyncrt_utils.h>
#include <cpprest/http_client.h>


namespace PluginLoader {
	namespace {
		std::unordered_map<std::wstring, HMODULE> loadedDll{};
	}

    namespace Requests {
        web::http::http_response sendPost(std::wstring url, std::wstring path, std::wstring data, int timeout) {
            web::http::client::http_client_config cfg;
            cfg.set_validate_certificates(false);
            cfg.set_timeout(utility::seconds(timeout));
            web::http::client::http_client client(url, cfg);
            return client.request(web::http::methods::POST, path, data).get();
        }

        std::string ReadSystemEnvironmentVariable(const std::string& variableName) {
            HKEY hKey;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
                printf("Failed to open registry key.\n");
                return ".";
            }

            char value[256];
            DWORD valueLength = sizeof(value);
            DWORD valueType;
            if (RegQueryValueExA(hKey, variableName.c_str(), NULL, &valueType, (LPBYTE)value, &valueLength) != ERROR_SUCCESS || valueType != REG_SZ) {
                RegCloseKey(hKey);
                printf("Failed to read registry value or value type mismatch.\n");
                return ".";
            }

            RegCloseKey(hKey);
            return std::string(value, valueLength - 1);
        }

        std::string GetSymbolCacheFileName() {
            auto pluginDir = ReadSystemEnvironmentVariable("TLG_DIRECTORY");
            if (pluginDir == ".") return "";

            if (pluginDir.ends_with('\\') || pluginDir.ends_with('/')) {
                pluginDir = pluginDir.substr(0, pluginDir.length() - 1);
            }
            return std::format("{}/symbols_cache.json", pluginDir);
        }

        std::unordered_map<std::string, std::string> GetIl2cppSymbolMapData() {
            try {
                const auto symbolCacheFileName = GetSymbolCacheFileName();

                auto resp = sendPost(L"http://uma.chinosk6.cn", L"/api/notice/get_il2cpp_map", L"", 30);
                if (resp.status_code() != 200) {
                    printf("Load event faield: %d\n", resp.status_code());
                    return {};
                }
                const auto respStr = resp.extract_utf8string().get();

                if (!symbolCacheFileName.empty()) {
                    std::ofstream outFile(symbolCacheFileName);
                    if (outFile) {
                        outFile << respStr;
                        outFile.close();
                    }
                }

                auto symbols = nlohmann::json::parse(respStr);
                std::unordered_map<std::string, std::string> map = symbols.get<std::unordered_map<std::string, std::string>>();
                return map;
            }
            catch (std::exception& e) {
                printf("GetIl2cppSymbolMapData failed: %s\n", e.what());
            }
        }

        std::unordered_map<std::string, std::string> GetIl2cppSymbolMapWithCache(HMODULE il2cpp_module) {
            try {
                const auto symbolCacheFileName = GetSymbolCacheFileName();
                if (symbolCacheFileName.empty()) {
                    goto FromServer;
                }

                if (std::filesystem::exists(symbolCacheFileName)) {
                    std::ifstream inFile(symbolCacheFileName);
                    if (!inFile.is_open()) {
                        std::cerr << "Failed to open file: " << symbolCacheFileName << std::endl;
                        goto FromServer;
                    }

                    nlohmann::json jsonObj;
                    try {
                        inFile >> jsonObj;
                    }
                    catch (const nlohmann::json::parse_error& e) {
                        std::cerr << "Error parsing JSON: " << e.what() << std::endl;
                        goto FromServer;
                    }

                    try {
                        std::unordered_map<std::string, std::string> map = jsonObj.get<std::unordered_map<std::string, std::string>>();

                        auto testProc = GetProcAddress(il2cpp_module, map["il2cpp_domain_get"].c_str());
                        if (testProc) {
                            return map;
                        }
                    }
                    catch (const nlohmann::json::type_error& e) {
                        std::cerr << "Error converting JSON to unordered_map: " << e.what() << std::endl;
                        goto FromServer;
                    }

                }

            }
            catch (std::exception& e) {
                printf("GetIl2cppSymbolMap From Cache failed: %s\n", e.what());
            }
        FromServer:
            auto serverData = GetIl2cppSymbolMapData();
            auto testProc = GetProcAddress(il2cpp_module, serverData["il2cpp_domain_get"].c_str());
            if (testProc) {
                return serverData;
            }
            MessageBoxW(NULL, L"由于游戏更新，插件目前无法加载符号，请等待服务端更新。\nDue to the game update, the plugin is currently unable to load the symbols, please wait for the server update!", 
                L"Error", MB_OK);
            return {};
        }
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

    std::string GetEnvVariable(const std::string& varName) {
        // 预先设置缓冲区大小
        DWORD bufferSize = 32767; // 最大环境变量大小
        char buffer[32767];

        // 调用 GetEnvironmentVariable 读取环境变量
        DWORD charsWritten = GetEnvironmentVariable(varName.c_str(), buffer, bufferSize);
        if (charsWritten == 0) {
            // 如果返回 0，意味着变量不存在或发生错误
            std::cerr << "Error retrieving environment variable: " << varName << std::endl;
            return "";
        }

        return std::string(buffer, charsWritten);
    }

    std::string ReadSystemEnvironmentVariable(const std::string& variableName) {
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
            printf("Failed to open registry key.\n");
            return "";
        }

        char value[256];
        DWORD valueLength = sizeof(value);
        DWORD valueType;
        if (RegQueryValueExA(hKey, variableName.c_str(), NULL, &valueType, (LPBYTE)value, &valueLength) != ERROR_SUCCESS || valueType != REG_SZ) {
            RegCloseKey(hKey);
            printf("Failed to read registry value or value type mismatch.\n");
            return "";
        }

        RegCloseKey(hKey);
        return std::string(value, valueLength - 1);
    }

    bool WriteSystemEnvironmentVariable(const std::string& variableName, const std::string& variableValue) {
        HKEY hKey;
        // 打开注册表键
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
            printf("Failed to open registry key for writing.\n");
            return false;
        }

        // 写入环境变量值
        if (RegSetValueExA(hKey, variableName.c_str(), 0, REG_SZ,
            reinterpret_cast<const BYTE*>(variableValue.c_str()),
            static_cast<DWORD>(variableValue.size() + 1)) != ERROR_SUCCESS) {
            RegCloseKey(hKey);
            printf("Failed to write registry value.\n");
            return false;
        }

        // 关闭注册表键
        RegCloseKey(hKey);

        // 通知系统环境变量已更改
        if (!SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
            reinterpret_cast<LPARAM>("Environment"),
            SMTO_ABORTIFHUNG, 5000, nullptr)) {
            printf("Failed to broadcast environment change message.\n");
            return false;
        }

        return true;
    }


    std::string GetTlgPath() {
        auto ret = ReadSystemEnvironmentVariable("TLG_DIRECTORY");
        if (ret.ends_with('\\') || ret.ends_with('/')) {
            ret = ret.substr(0, ret.length() - 1);
        }
        return ret;
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
        std::filesystem::path tlgFile = L"tlg.dll";
        auto tlgInstallPath = std::filesystem::exists(tlgFile) ? "." : GetTlgPath();
        bool needWriteEnv = false;
        
        if (tlgInstallPath.empty()) {
            auto select = MessageBoxW(NULL, L"Please Select Your TLG.DLL file.", L"Installation location not found", MB_OKCANCEL);
            if (select != IDOK) return;
            const auto selectedFiles = SelectFiles();
            if (selectedFiles.size() != 1) {
                return CheckAndLoadDll();
            }
            tlgFile = selectedFiles[0];
            needWriteEnv = true;
        }
        else {
            tlgFile = tlgInstallPath / tlgFile;
        }

        auto tlgModule = LoadLibraryW(tlgFile.c_str());
        if (!tlgModule) {
            TerminateProcess(GetCurrentProcess(), 0);
        }
        if (!FindIdentifierInModule(tlgModule, "foramghl97")) {
            TerminateProcess(GetCurrentProcess(), 0);
        }
        if (needWriteEnv) {
            WriteSystemEnvironmentVariable("TLG_DIRECTORY", (tlgFile.parent_path() / "").string());
        }
#endif
	}

}
