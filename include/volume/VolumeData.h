#pragma once

#include <memory>

namespace vol
{

struct VolumeData
{
	int width = 0;
	int height = 0;
	int depth = 0;

	std::unique_ptr<unsigned char[]> rgba = nullptr;

}; // VolumeData

}