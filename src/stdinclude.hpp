#pragma once

#define NOMINMAX

#include <Windows.h>
#include <shlobj.h>

#include <cinttypes>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <locale>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <thread>
#include <variant>

#include <exception>
#include <vector>
#include <regex>

#include <MinHook.h>

#include <rapidjson/document.h>
#include <rapidjson/encodings.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/writer.h>

#include "il2cpp/il2cpp_symbols.hpp"
#include "pluginLoader/pluginLoader.hpp"
#include <nlohmann/json.hpp>


extern bool g_has_config_file;
extern bool g_enable_console;
extern bool g_confirm_every_times;
extern std::vector<std::string> g_pluginPath;
