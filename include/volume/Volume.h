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

	VolumeDesc m_desc;

}; // Volume

}