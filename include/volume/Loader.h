#pragma once

#include <string>

namespace vol
{

struct VolumeData;

class Loader
{
public:
	static bool Load(VolumeData& data, const std::string& filepath);

}; // Loader

}