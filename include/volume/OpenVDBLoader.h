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
	static bool Load(VolumeData& data, const std::string& filepath);

	static std::shared_ptr<Volume> 
		LoadFromFile(const std::string& filepath);
	
}; // OpenVDBLoader

}