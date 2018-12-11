#include "volume/Loader.h"
#include "volume/RawLoader.h"
#include "volume/OpenVDBLoader.h"

#include <boost/filesystem.hpp>

namespace vol
{

bool Loader::Load(VolumeData& data, const std::string& filepath)
{	
	auto ext = boost::filesystem::extension(filepath);
	if (ext == ".raw3d") {
		return RawLoader::Load(data, filepath);
	} else if (ext == ".vdb") {
		return OpenVDBLoader::Load(data, filepath);
	}

	return false;
}

}