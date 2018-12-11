#pragma once

#include "volume/VolumeDesc.h"

#include <openvdb/openvdb.h>

namespace vol
{

class Volume
{
public:
	Volume(const openvdb::FloatGrid::ConstPtr& grid);

private:
	void InitDesc();

private:
	openvdb::FloatGrid::ConstPtr m_grid;

	openvdb::Coord m_extents = {256, 256, 256};

	VolumeDesc m_desc;

}; // Volume

}