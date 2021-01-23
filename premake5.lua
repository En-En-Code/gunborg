workspace "Gunborg"
    architecture "x86_64"

    configurations
	{
		"Debug",
		"Release",
		"Dist"
	}
	
    flags
	{
		"MultiProcessorCompile"
    }
    
outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

project "Gunborg"
    location "Gunborg"
    kind "ConsoleApp"
	language "C++"
	cppdialect "C++17"
    staticruntime "on"
    
	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.cpp"
	}

	includedirs
	{
		"Gunborg/src",
	}

	filter "system:windows"
		systemversion "latest"
		
	filter "configurations:Debug"
		defines "GB_DEBUG"
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		defines "GB_RELEASE"
		runtime "Release"
		optimize "on"

	filter "configurations:Dist"
		defines "GB_DIST"
		runtime "Release"
		optimize "on"