#pragma once

#include <string>

namespace vol
{

struct VolumeData;

class RawLoader
{
public:
	static bool Load(VolumeData& data, const std::string& filepath);

}; // RawLoader

}