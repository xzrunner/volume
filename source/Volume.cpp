#include "volume/Volume.h"

namespace vol
{

Volume::Volume(const openvdb::FloatGrid::ConstPtr& grid)
	: m_grid(grid)
{
	grid->print();
}

void Volume::InitDesc()
{
	//m_desc.voxel_count = m_extents.x() * m_extents.y() * m_extents.z();
	//m_desc.width  = m_extents.x();
	//m_desc.height = m_extents.y();
	//m_desc.depth  = m_extents.z();
	m_desc.format = 20;
}

}