#include "vc/core/types/VolumeZARR.hpp"

#include <thread>

#include "xtensor/xmanipulation.hpp"
#include "xtensor/xview.hpp"
#include "z5/factory.hxx"
#include "z5/multiarray/xtensor_access.hxx"
#include "z5/attributes.hxx"

namespace fs = volcart::filesystem;

using namespace volcart;

// Load a Volume from disk
VolumeZARR::VolumeZARR(fs::path path) : Volume(std::move(path)), zarrFile_(path)
{
    format_ = ZARR;
    zarrFile_ = z5::filesystem::handle::File(path_);
    z5::filesystem::handle::Group group(fs::path(path_), z5::FileMode::FileMode::r);
    z5::readAttributes(group, groupAttr_);
}

// Setup a Volume from a folder of slices
VolumeZARR::VolumeZARR(fs::path path, std::string uuid, std::string name)
    : Volume(std::move(path), std::move(uuid), std::move(name)),
      zarrFile_(path)
{
    format_ = ZARR;
}

auto VolumeZARR::getSlicePath(int index) const -> fs::path
{
    // There is no single path for a chunked format
    return fs::path();
}

auto VolumeZARR::getSliceData(int index, VolumeAxis axis) const -> cv::Mat
{       
    // Not implemented yet}
    return cv::Mat();
}

auto VolumeZARR::getSliceDataRect(int index, cv::Rect rect, VolumeAxis axis) const -> cv::Mat
{
    return load_slice_(index, rect, axis);
}

void VolumeZARR::setSliceData(int index, const cv::Mat& slice, bool compress)
{
    // Not implemented yet
}

auto VolumeZARR::getZarrLevels() const -> std::vector<int>
{ 
    std::vector<std::string> keys;
    zarrFile_.keys(keys);
    std::sort(keys.begin(), keys.end());
    std::vector<int> levels;
    for(auto key : keys) {
        levels.push_back(std::stoi(key));
    }
    return levels;
}

auto VolumeZARR::getScaleForLevel(int level) const -> float
{
    return groupAttr_["multiscales"][0]["datasets"][level]["coordinateTransformations"][0]["scale"][0].get<float>();
}

void VolumeZARR::putCacheChunk(z5::types::ShapeType chunkId, void* chunk, z5::types::ShapeType chunkShape, std::size_t chunkSize) const
{
    if (!caches_[zarrLevel_]->contains(chunkId)) {
        CacheEntry entry = {*static_cast<std::vector<std::uint16_t>*>(chunk), chunkShape, chunkSize};
        caches_[zarrLevel_]->put(chunkId, entry);
    }
}

void* VolumeZARR::getCacheChunk(z5::types::ShapeType chunkId, z5::types::ShapeType& chunkShape, std::size_t& chunkSize) const
{
    if (caches_[zarrLevel_]->contains(chunkId)) {
        auto entry = caches_[zarrLevel_]->getPointer(chunkId);
        chunkShape = entry->shape;
        chunkSize = entry->size;
        return &entry->data;
    } else {
        return nullptr;
    }
}

auto VolumeZARR::load_slice_(int index, cv::Rect2i rect, VolumeAxis axis) const -> cv::Mat
{
    {
        std::unique_lock<std::shared_mutex> lock(print_mutex_);
        std::cout << "Requested to load slice " << index << ", level " << zarrLevel_ << std::endl;
    }

    auto itDs = zarrDs_.find(zarrLevel_);
    if (itDs != zarrDs_.end()) {

        xt::xtensor<std::uint16_t, 3>* data = nullptr;
        auto offsetShape = (axis == Z)   ? z5::types::ShapeType({(size_t)index, (size_t)rect.y, (size_t)rect.x})
                           : (axis == X) ? z5::types::ShapeType({(size_t)rect.x, (size_t)index, (size_t)rect.y})
                                         : z5::types::ShapeType({(size_t)rect.x, (size_t)rect.y, (size_t)index});
        // std::cout << "Offset Shape: " << offsetShape[0] << ", " << offsetShape[1] << ", " << offsetShape[2] << std::endl;

        // Prepare desired read shape to control how many slices get loaded
        auto shape = itDs->second->shape();
        // in a single chunk
        xt::xtensor<std::uint16_t, 3>::shape_type tensorShape;
        size_t maxWidth = shape[2] - rect.x;
        size_t maxHeight = shape.at(1) - rect.y;
        tensorShape =
            (axis == Z) ? xt::xtensor<std::uint16_t, 3>::shape_type(
                              {1, std::min((size_t)rect.height, maxHeight), std::min((size_t)rect.width, maxWidth)})
            : (axis == X) ? xt::xtensor<std::uint16_t, 3>::shape_type(
                                {std::min((size_t)rect.height, maxHeight), 1, std::min((size_t)rect.width, maxWidth)})
                          : xt::xtensor<std::uint16_t, 3>::shape_type(
                                {std::min((size_t)rect.height, maxHeight), std::min((size_t)rect.width, maxWidth), 1});

        // Read data
        data = new xt::xtensor<std::uint16_t, 3>(tensorShape);
        int threads = static_cast<int>(std::thread::hardware_concurrency());

        // auto t1 = std::chrono::high_resolution_clock::now();
        try {
            z5::multiarray::readSubarray<std::uint16_t>(*itDs->second, *data, offsetShape.begin(), threads);
        } catch (std::runtime_error) {
            std::cout << "Runtime error in readSubarray()" << std::endl;
        }
        // auto t2 = std::chrono::high_resolution_clock::now();
        // std::cout << "Duration: " << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count() <<
        // std::endl;

        // Adjust axis
        if (axis == X) {
            return cv::Mat::zeros(100, 100, CV_8UC1);
            // std::cout << "Shape Original: " << data->shape()[0] << ", " << data->shape()[1] << ", " << data->shape()[2]
            //           << std::endl;
            // *data = xt::view(xt::swapaxes(*data, 0, 1), 1);
            // std::cout << "Shape After: " << data->shape()[0] << ", " << data->shape()[1] << ", " << data->shape()[2]
            //           << std::endl;
            // return cv::Mat(data->shape()[0], data->shape()[2], CV_16U, data->data(), 0);
        } else if (axis == Y) {
            return cv::Mat::zeros(100, 100, CV_8UC1);
            // std::cout << "Shape Original: " << data->shape()[0] << ", " << data->shape()[1] << ", " << data->shape()[2]
            //           << std::endl;
            // *data = xt::swapaxes(*data, 1, 2);
            // *data = xt::swapaxes(*data, 0, 1);
            // std::cout << "Shape Middle: " << data->shape()[0] << ", " << data->shape()[1] << ", " << data->shape()[2]
            //           << std::endl;
            // *data = xt::view(*data, 1);
            // std::cout << "Shape After: " << data->shape()[0] << ", " << data->shape()[1] << ", " << data->shape()[2]
            //           << std::endl;
            // return cv::Mat(data->shape()[0], data->shape()[2], CV_16U, data->data(), 0);
        } else if (axis == Z) {
            // std::cout << "Shape After: " << data->shape()[0] << ", " << data->shape()[1] << ", " << data->shape()[2]
            //           << std::endl;
            return cv::Mat(data->shape()[1], data->shape()[2], CV_16U, data->data(), 0);
        }

        // auto view = xt::view(*data, index % chunkShape[(int)axis]);
        // auto view = xt::view(*data, index % chunkShape[(int)axis]);
        // return cv::Mat(view.shape()[0], view.shape()[1], CV_16U, view.data() + view.data_offset(), 0);
        return cv::Mat(data->shape()[1], data->shape()[2], CV_16U, data->data(), 0);
    }

    // No valid data set or axis => return empty
    return cv::Mat();
}

void VolumeZARR::openZarr()
{
    for(auto level : getZarrLevels()) {
        z5::filesystem::handle::Dataset hndl(
            fs::path(path_ / std::to_string(level)), z5::FileMode::FileMode::r);
        hndl.setZarrDelimiter(std::string("/"));
        zarrDs_[level] = z5::filesystem::openDataset(hndl);

        if (level == 0) {
            slices_ = zarrDs_[level]->shape()[0];
            width_ = zarrDs_[level]->shape()[1];
            height_ = zarrDs_[level]->shape()[2];
        }
        
        caches_[level] = DefaultCache::New(1000L);

        zarrDs_[level]->enableCaching(true,
            [&](z5::types::ShapeType chunkId, void* chunk, z5::types::ShapeType shape, std::size_t size) -> void { putCacheChunk(chunkId, chunk, shape, size); },
            [&](z5::types::ShapeType chunkId, z5::types::ShapeType& shape, std::size_t& size) -> void* { return getCacheChunk(chunkId, shape, size); });
    }
}

void VolumeZARR::cachePurge() const
{
    for(auto cache : caches_) {
        cache.second->purge();
    }
}