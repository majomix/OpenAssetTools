Cooker = {}

function Cooker:include(includes)
	if includes:handle(self:name()) then
		includedirs {
			path.join(ProjectFolder(), "CookerCli")
		}
	end
end

function Cooker:link(links)

end

function Cooker:use()
	dependson(self:name())
end

function Cooker:name()
	return "Cooker"
end

function Cooker:project()
	local folder = ProjectFolder()
	local includes = Includes:create()
	local links = Links:create()

	project(self:name())
        targetdir(TargetDirectoryBin)
		targetname "Cooker"
		location "%{wks.location}/src/%{prj.name}"
		kind "ConsoleApp"
		language "C++"

		files {
			path.join(folder, "CookerCli/**.h"),
			path.join(folder, "CookerCli/**.cpp")
		}

		self:include(includes)
		Utils:include(includes)
		ZoneCommon:include(includes)
		Common:include(includes)
		Cryptography:include(includes)
		zlib:include(includes)
		salsa20:include(includes)
		libtomcrypt:include(includes)

		links:linkto(Utils)
		links:linkto(ZoneCommon)
		links:linkto(Common)
		links:linkto(Cryptography)
		links:linkto(zlib)
		links:linkto(salsa20)
		links:linkto(libtomcrypt)
		links:linkall()
end
