#pragma once

#include <memory>
#include <string>

namespace vol
{

class Volume;
struct VolumeData;

class OpenVDBLoader
{
public:
	static bool Load(VolumeData& data, const std::string& filepath, float import_scale = 1.0f);

	static std::shared_ptr<Volume> 
		LoadFromFile(const std::string& filepath);
	
}; // OpenVDBLoader

}