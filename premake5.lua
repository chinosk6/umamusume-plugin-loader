dependencies = {
	basePath = "./deps"
}

function dependencies.load()
	dir = path.join(dependencies.basePath, "premake/*.lua")
	deps = os.matchfiles(dir)

	for i, dep in pairs(deps) do
		dep = dep:gsub(".lua", "")
		require(dep)
	end
end

function dependencies.imports()
	for i, proj in pairs(dependencies) do
		if type(i) == 'number' then
			proj.import()
		end
	end
end

function dependencies.projects()
	for i, proj in pairs(dependencies) do
		if type(i) == 'number' then
			proj.project()
		end
	end
end

include "deps/minhook.lua"
include "deps/rapidjson.lua"
include "build/conanbuildinfo.premake.lua"

workspace "umamusume-plugin-loader"
	conan_basic_setup()

	location "./build"
	objdir "%{wks.location}/obj"
	targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}"

	architecture "x64"
	platforms "x64"

	configurations {
		"Debug",
		"Release",
	}

	buildoptions {
		"/std:c++latest",
		"/utf-8",
	}
	systemversion "latest"
	symbols "On"
	staticruntime "On"
	editandcontinue "Off"
	warnings "Off"
	characterset "ASCII"

	flags {
		"NoIncrementalLink",
		"NoMinimalRebuild",
		"MultiProcessorCompile",
	}

	staticruntime "Off"

	configuration "Release"
		optimize "Full"
		buildoptions "/Os"

	configuration "Debug"
		optimize "Debug"

	dependencies.projects()

	project "umamusume-plugin-loader"
		targetname "version"

		language "C++"
		kind "SharedLib"

		files {
			"./src/**.hpp",
			"./src/**.cpp",
			"./src/**.asm",
			"./src/**.def",
		}

		includedirs {
			"./src",
			"%{prj.location}/src",
		}

		dependencies.imports()

		linkoptions { conan_sharedlinkflags }

		configuration "Release"
			linkoptions "/SAFESEH:NO"
			syslibdirs {
				"./libs/Release",
			}

		configuration "Debug"
			linkoptions "/SAFESEH:NO"
			syslibdirs {
				"./libs/Debug",
			}
