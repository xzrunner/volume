#pragma once

namespace vol
{

struct VolumeDesc
{
	int voxel_count = 0;
	int width = 0;
	int height = 0;
	int depth = 0;
	int format = 0;
	float min_value = 0;
	float max_value = 0;
	float x_scale = 0;
	float y_scale = 0;
	float z_scale = 0;

}; // VolumeDesc

}