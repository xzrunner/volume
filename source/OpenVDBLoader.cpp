#define NOMINMAX

#include "volume/OpenVDBLoader.h"
#include "volume/Volume.h"
#include "volume/VolumeData.h"

#include <openvdb/openvdb.h>
#include <openvdb/tools/Interpolation.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>

#include <limits>

namespace
{

// code from OpenVDBForUnity
// https://github.com/karasusan/OpenVDBForUnity

template <typename RealType>
struct ValueRange {
public:
    ValueRange()
            : m_min(std::numeric_limits<RealType>::max())
            , m_max(std::numeric_limits<RealType>::min())
    {}
    ValueRange(RealType min_, RealType max_)
            : m_min(min_)
            , m_max(max_)
    {}

    RealType getMin() const { return m_min; }
    RealType getMax() const { return m_max; }

    void addValue(RealType value)
    {
        m_min = std::min(m_min, value);
        m_max = std::max(m_max, value);
    }

private:
    RealType m_min, m_max;
};

typedef ValueRange<float> FloatRange;

template <typename T>
struct identity { using type = T; };

template <typename T> inline
T unlerp(typename identity<T>::type a, typename identity<T>::type b, T x)
{
	return (x - a) / (b - a);
}

inline openvdb::CoordBBox get_index_space_bounding_box(const openvdb::GridBase& grid)
{
    try
    {
        const auto file_bbox_min = openvdb::Coord(grid.metaValue<openvdb::Vec3i>("file_bbox_min"));
        if (file_bbox_min.x() == std::numeric_limits<int>::max() ||
            file_bbox_min.y() == std::numeric_limits<int>::max() ||
            file_bbox_min.z() == std::numeric_limits<int>::max()) {
            return {};
        }
        const auto file_bbox_max = openvdb::Coord(grid.metaValue<openvdb::Vec3i>("file_bbox_max"));

        if (file_bbox_max.x() == std::numeric_limits<int>::min() ||
            file_bbox_max.y() == std::numeric_limits<int>::min() ||
            file_bbox_max.z() == std::numeric_limits<int>::min()) {
            return {};
        }

        return { file_bbox_min, file_bbox_max };
    }
    catch (openvdb::Exception e)
    {
        return {};
    }
}

template <typename SamplingFunc, typename RealType>
bool sample_volume( const openvdb::Coord& extents, SamplingFunc sampling_func, FloatRange& out_value_range, RealType* out_samples)
{
    const auto domain = openvdb::CoordBBox(openvdb::Coord(0, 0, 0), extents - openvdb::Coord(1, 1, 1));
    if (domain.empty())
    {
        return false;
    }
    const auto num_voxels = domain.volume();

    // Sample on a lattice.
    typedef tbb::enumerable_thread_specific<FloatRange> PerThreadRange;
    PerThreadRange ranges;
    const openvdb::Vec3i stride = {1, extents.x(), extents.x() * extents.y()};
    tbb::atomic<bool> cancelled;
    cancelled = false;
    tbb::parallel_for(
            domain,
            [&sampling_func, &stride, &ranges, out_samples, &cancelled] (const openvdb::CoordBBox& bbox)
            {
                const auto local_extents = bbox.extents();

                // Loop through local bbox.
                PerThreadRange::reference this_thread_range = ranges.local();
                for (auto z = bbox.min().z(); z <= bbox.max().z(); ++z)
                {
                    for (auto y = bbox.min().y(); y <= bbox.max().y(); ++y)
                    {
                        for (auto x = bbox.min().x(); x <= bbox.max().x(); ++x)
                        {
                            const auto domain_index = openvdb::Vec3i(x, y, z);
                            const auto linear_index = domain_index.dot(stride) * 4;
                            const auto sample_value = sampling_func(domain_index);

                            // fixme
                            out_samples[linear_index + 0] = sample_value;
                            out_samples[linear_index + 1] = sample_value;
                            out_samples[linear_index + 2] = sample_value;
                            out_samples[linear_index + 3] = sample_value;
                            this_thread_range.addValue(sample_value);
                        }
                    }
                }
            });

    // Merge per-thread value ranges.
    out_value_range = FloatRange();
    for (const FloatRange& per_thread_range : ranges) {
        out_value_range.addValue(per_thread_range.getMin());
        out_value_range.addValue(per_thread_range.getMax());
    }

    // Remap sample values to [0, 1].
    int size = num_voxels * 4;
    typedef tbb::blocked_range<size_t> tbb_range;
    tbb::parallel_for(tbb_range(0, size), [out_samples, &out_value_range](const tbb_range& range)
    {
        for (auto i = range.begin(); i < range.end(); ++i)
        {
            out_samples[i] = unlerp( out_value_range.getMin(), out_value_range.getMax(), out_samples[i]);
        }
    });
}

template <typename RealType>
bool sample_grid(
        const openvdb::FloatGrid& grid,
        const openvdb::Coord& sampling_extents,
        FloatRange& value_range,
        openvdb::Vec3d& scale,
        RealType* out_data)
{
    assert(out_data);

    const auto grid_bbox_is = get_index_space_bounding_box(grid);
    const auto bbox_world = grid.transform().indexToWorld(grid_bbox_is);

    scale = bbox_world.extents();

    // Return if the grid bbox is empty.
    if (grid_bbox_is.empty())
    {
        return false;
    }

    const auto domain_extents = sampling_extents.asVec3d();
    openvdb::tools::GridSampler<openvdb::FloatGrid, openvdb::tools::BoxSampler> sampler(grid);

    auto sampling_func = [&sampler, &bbox_world, &domain_extents] (const openvdb::Vec3d& domain_index) -> RealType
    {
        const auto sample_pos_ws = bbox_world.min() + (domain_index + 0.5) / domain_extents * bbox_world.extents();
        return sampler.wsSample(sample_pos_ws);
    };

    sample_volume( sampling_extents, sampling_func, value_range, out_data);
}

}

namespace vol
{

bool OpenVDBLoader::Load(VolumeData& data, const std::string& filepath)
{
	openvdb::initialize();

	openvdb::io::File file(filepath);
	try {
		bool new_file = file.open();
	} catch (openvdb::Exception& e) {
		printf("err: %s\n", e.what());
		return false;
	}

//	auto archive = file.copy();
	auto grids_ptr = file.getGrids();
	openvdb::GridCPtrVec grids;
	grids.insert(grids.end(), grids_ptr->begin(), grids_ptr->end());

	file.close();

	if (grids.empty()) {
		return false;
	}

	auto grid = openvdb::gridConstPtrCast<openvdb::FloatGrid>(grids[0]);
	openvdb::Coord extents = { 256, 256, 256 };

	grid->print();

	// sample

//	openvdb::Coord extents{ extents.x(), extents.y(), extents.z() };

	FloatRange value_range;
	openvdb::Vec3d scale;
	float* buffer = new float[extents.x() * extents.y() * extents.z() * 4];
	sample_grid(*openvdb::gridConstPtrCast<openvdb::FloatGrid>(grid), extents, value_range, scale, buffer);
	//m_summary->min_value = value_range.getMin();
	//m_summary->max_value = value_range.getMax();

	//m_summary->x_scale = scale.x() * m_scaleFactor;
	//m_summary->y_scale = scale.y() * m_scaleFactor;
	//m_summary->z_scale = scale.z() * m_scaleFactor;

	const int w = extents.x();
	const int h = extents.y();
	const int d = extents.z();

	data.width  = w;
	data.height = h;
	data.depth  = d;
	const int n = w * h * d;
	auto alpha_buf = new char[n];
	memset(alpha_buf, n, 0);
	int ptr = 0;
	for (int i = 0; i < n; ++i, ptr += 4) {
		alpha_buf[i] = static_cast<char>(255 * buffer[ptr]);
	}
	data.buf.reset(alpha_buf);

	return true;
}

std::shared_ptr<Volume>
OpenVDBLoader::LoadFromFile(const std::string& filepath)
{
	openvdb::initialize();

	openvdb::io::File file(filepath);
	try {
		if (!file.open()) {
			return false;
		}
	} catch (openvdb::Exception& e) {
		printf("err: %s\n", e.what());
		return false;
	}

//	auto archive = file.copy();
	auto grids_ptr = file.getGrids();
	openvdb::GridCPtrVec grids;
	grids.insert(grids.end(), grids_ptr->begin(), grids_ptr->end());

	file.close();

	if (grids.empty()) {
		return nullptr;
	}

	auto grid = openvdb::gridConstPtrCast<openvdb::FloatGrid>(grids[0]);
//	openvdb::Coord extents = { 256, 256, 256 };
	return std::make_shared<Volume>(grid);
}

}