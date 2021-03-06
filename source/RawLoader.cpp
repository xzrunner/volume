#include "volume/RawLoader.h"
#include "volume/VolumeData.h"

#include <fs_file.h>
#include <cpputil/StringHelper.h>

#include <boost/filesystem.hpp>

namespace vol
{

bool RawLoader::Load(VolumeData& data, const std::string& filepath)
{
	if (!boost::filesystem::is_regular_file(filepath)) {
		return false;
	}

	auto filename = boost::filesystem::path(filepath).stem().string();
	std::vector<std::string> tokens;
	cpputil::StringHelper::Split(filename, "_", tokens);
	if (tokens.size() < 3) {
		return false;
	}

	const int sz = tokens.size();
	int w = std::stoi(tokens[sz - 3]);
	int h = std::stoi(tokens[sz - 2]);
	int d = std::stoi(tokens[sz - 1]);
	data.width  = w;
	data.height = h;
	data.depth  = d;

	const int n = w * h * d;

	auto alpha_buf = new unsigned char[n];
	if (!alpha_buf) {
		return false;
	}
	struct fs_file* file = fs_open(filepath.c_str(), "rb");
	if (!file) {
		return false;
	}
	fs_read(file, alpha_buf, n);
	fs_close(file);

	auto rgba_buf = new unsigned char[n * 4];
	for (int i = 0; i < n; ++i) {
		rgba_buf[i * 4 + 0] = alpha_buf[i];
		rgba_buf[i * 4 + 1] = alpha_buf[i];
		rgba_buf[i * 4 + 2] = alpha_buf[i];
		rgba_buf[i * 4 + 3] = alpha_buf[i];
	}
	data.rgba.reset(rgba_buf);

	delete[] alpha_buf;

	return true;
}

}