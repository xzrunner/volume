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
                            const auto volume_idx = openvdb::Vec3i(x, y, z);
                            const auto array_idx = volume_idx.dot(stride);
//                            const auto sample_value = -sampling_func(volume_idx);
							const auto sample_value = sampling_func(volume_idx);
                            out_samples[array_idx] = sample_value;
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
    int size = num_voxels;
    typedef tbb::blocked_range<size_t> tbb_range;
    tbb::parallel_for(tbb_range(0, size), [out_samples, &out_value_range](const tbb_range& range)
    {
        for (auto i = range.begin(); i < range.end(); ++i)
        {
            out_samples[i] = unlerp( out_value_range.getMin(), out_value_range.getMax(), out_samples[i]);
        }
    });

    return true;
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

    return sample_volume(sampling_extents, sampling_func, value_range, out_data);
}

}

namespace vol
{

bool OpenVDBLoader::Load(VolumeData& data, const std::string& filepath, float import_scale)
{
	openvdb::initialize();

	openvdb::io::File file(filepath);
	try {
		bool new_file = file.open();
	} catch (openvdb::Exception& e) {
		printf("err: %s\n", e.what());
		return false;
	}

	auto grids_ptr = file.getGrids();
	openvdb::GridCPtrVec grids;
	grids.insert(grids.end(), grids_ptr->begin(), grids_ptr->end());

	file.close();

	if (grids.empty()) {
		return false;
	}

	// log
	for (auto& grid : grids) {
		grid->print();
	}

	auto grid = openvdb::gridConstPtrCast<openvdb::FloatGrid>(grids[0]);

	const auto grid_bbox_is = get_index_space_bounding_box(*grid);
	const auto w = static_cast<int>(std::ceil((grid_bbox_is.max().x() - grid_bbox_is.min().x() + 1) * import_scale));
	const auto h = static_cast<int>(std::ceil((grid_bbox_is.max().y() - grid_bbox_is.min().y() + 1) * import_scale));
	const auto d = static_cast<int>(std::ceil((grid_bbox_is.max().z() - grid_bbox_is.min().z() + 1) * import_scale));
	openvdb::Coord extents = { w, h, d };

	// sample

	const int n = w * h * d;
	data.width  = w;
	data.height = h;
	data.depth  = d;

	auto rgba_buf = new unsigned char[n * 4];
	memset(rgba_buf, n * 4, 0);
	if (grids.size() == 4)
	{
		FloatRange value_range;
		openvdb::Vec3d scale;
		float* channel_buf = new float[n];
		for (int i = 0; i < 4; ++i)
		{
			sample_grid(*openvdb::gridConstPtrCast<openvdb::FloatGrid>(grid), extents, value_range, scale, channel_buf);
			for (int j = 0; j < n; ++j) {
				rgba_buf[j * 4 + i] = static_cast<unsigned char>(255 * channel_buf[j]);
			}
		}

		delete[] channel_buf;
	}
	else
	{
		FloatRange value_range;
		openvdb::Vec3d scale;
		float* alpha_buf = new float[n];
		sample_grid(*openvdb::gridConstPtrCast<openvdb::FloatGrid>(grid), extents, value_range, scale, alpha_buf);
		for (int i = 0; i < n; ++i)
		{
			const unsigned char alpha = static_cast<unsigned char>(255 * alpha_buf[i]);
			rgba_buf[i * 4 + 0] = alpha;
			rgba_buf[i * 4 + 1] = alpha;
			rgba_buf[i * 4 + 2] = alpha;
			rgba_buf[i * 4 + 3] = alpha;
		}
		delete[] alpha_buf;
	}
	data.rgba.reset(rgba_buf);

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

	auto grids_ptr = file.getGrids();
	openvdb::GridCPtrVec grids;
	grids.insert(grids.end(), grids_ptr->begin(), grids_ptr->end());

	file.close();

	if (grids.empty()) {
		return nullptr;
	}

	auto grid = openvdb::gridConstPtrCast<openvdb::FloatGrid>(grids[0]);
	return std::make_shared<Volume>(grid);
}

}