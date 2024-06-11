// COverlayHandler.cpp
// Philip Allgaier 2024 May
#include "COverlayHandler.hpp"

#include "CVolumeViewer.hpp"
#include "vc/core/io/OBJReader.hpp"
#include "vc/core/io/PLYReader.hpp"
#include "vc/core/types/ITKMesh.hpp"

using namespace ChaoVis;
namespace vc = volcart;
namespace fs = std::filesystem;

COverlayHandler::COverlayHandler(CVolumeViewer* volumeViewer) : viewer(volumeViewer)
{}

auto roundDownToNearestMultiple(float numToRound, int multiple)  ->int
{
    return ((static_cast<int>(numToRound) / multiple) * multiple);
}

void COverlayHandler::setOverlaySettings(OverlaySettings overlaySettings)
{ 
    settings = overlaySettings; 

    // Hard-code for testing
    settings.offset = -125;
    settings.xAxis = 2;
    settings.yAxis = 0;
    settings.zAxis = 1;
    settings.scale = 4;
    settings.chunkSize = 25;
}

auto COverlayHandler::determineChunksForView() const -> OverlayChunkIDs
{
    OverlayChunkIDs res;
    
    if (settings.path.isEmpty()) {
        return {};
    }

    // Get the currently displayed region
    auto rect = viewer->GetView()->mapToScene(viewer->GetView()->viewport()->rect());
 
    auto xIndexStart = std::max(100, roundDownToNearestMultiple((rect.first().x() - 100) / settings.scale, settings.chunkSize) - settings.offset);
    xIndexStart -= settings.chunkSize; // due to the fact that file 000100 contains from -100 to 100, 000125 contains from 0 to 200, 000150 from 100 to 300
    auto yIndexStart = std::max(100, roundDownToNearestMultiple((rect.first().y() - 100) / settings.scale, settings.chunkSize) - settings.offset);
    yIndexStart -= settings.chunkSize;

    auto imageIndex = viewer->GetImageIndex();
    auto zIndexEnd = std::max(100, roundDownToNearestMultiple((imageIndex - 100) / settings.scale, settings.chunkSize) - settings.offset);
    auto zIndexStart = zIndexEnd - settings.chunkSize;

    auto xIndexEnd = roundDownToNearestMultiple((rect.at(2).x() - 100) / settings.scale, settings.chunkSize) - settings.offset;
    auto yIndexEnd = roundDownToNearestMultiple((rect.at(2).y() - 100) / settings.scale, settings.chunkSize) - settings.offset;

    OverlayChunkID id;
    for (auto z = zIndexStart; z <= zIndexEnd; z += settings.chunkSize) {
        for (auto x = xIndexStart; x <= xIndexEnd; x += settings.chunkSize) {
            for (auto y = yIndexStart; y <= yIndexEnd; y += settings.chunkSize) {
                id[settings.xAxis] = x;
                id[settings.yAxis] = y;
                id[settings.zAxis] = z;

                res.push_back(id);
            }
        }
    }

    return res;
}

auto COverlayHandler::determineNotLoadedOverlayFiles() const -> OverlayChunkFiles
{
    auto chunks = determineChunksForView();

    QString folder;
    OverlayChunkFiles fileList;
    QDir overlayMainFolder(settings.path);
    auto absPath = overlayMainFolder.absolutePath();

    for (auto chunk : chunks) {
        if (chunkData.find(chunk) == chunkData.end()) {
            // TODO:Check if the settings logic for axis really works here
            folder = QStringLiteral("%1")
                         .arg(chunk[settings.yAxis], 6, 10, QLatin1Char('0'))
                         .append("_" + QStringLiteral("%1").arg(chunk[settings.zAxis], 6, 10, QLatin1Char('0')))
                         .append("_" + QStringLiteral("%1").arg(chunk[settings.xAxis], 6, 10, QLatin1Char('0')));

            QDir overlayFolder(absPath + QDir::separator() + folder);
            QStringList files = overlayFolder.entryList({"*.ply", "*.obj"}, QDir::NoDotAndDotDot | QDir::Files);

            for (auto file : files) {
                file = overlayFolder.path() + QDir::separator() + file;
                fileList[chunk].push_back(file);
            }
        }
    }

    return fileList;
}

void COverlayHandler::loadOverlayData(OverlayChunkFiles chunksToLoad)
{
    if (chunksToLoad.size() == 0 || settings.path.isEmpty()) {
        return;
    }

    vc::ITKMesh::Pointer mesh;
    itk::Point<double, 3> point;
    threadData.clear();

    // Convert to flat work list for threads
    std::vector<OverlayChunkID> chunks;
    std::vector<QString> fileNames;
    for (auto chunk : chunksToLoad) {
        for (auto file : chunk.second) {
            chunks.push_back(chunk.first);
            fileNames.push_back(file);
        }
    }

    int numThreads = static_cast<int>(std::thread::hardware_concurrency());
    int jobSize = 5;
    for (int f = 0; f < fileNames.size(); f += numThreads * jobSize) {
        std::vector<std::thread> threads;

        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([=]() {
                for (int j = 0; j < jobSize && (f + i * jobSize + j) < fileNames.size(); ++j) {
                    loadSingleOverlayFile(fileNames.at(f + i * jobSize + j), chunks.at(f + i * jobSize + j), i);
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }
    }

    for (auto threadDataSet : threadData) {
        if (threadDataSet.second.size() > 0) {
            mergeThreadData(threadDataSet.second);
        }
    }
}

void COverlayHandler::loadSingleOverlayFile(const QString& file, OverlayChunkID chunkID, int threadNum) const
{
    vc::ITKMesh::Pointer mesh;
    itk::Point<double, 3> point;

    //std::cout << file.toStdString() << std::endl;
    if (file.endsWith(".ply")) {
        volcart::io::PLYReader reader(fs::path(file.toStdString()));
        reader.read();
        mesh = reader.getMesh();
    } else if (file.endsWith(".obj")) {
        volcart::io::OBJReader reader;
        reader.setPath(file.toStdString());
        reader.read();
        mesh = reader.getMesh();
    } else {
        return;
    }

    auto numPoints = mesh->GetNumberOfPoints();

    for (std::uint64_t pnt_id = 0; pnt_id < numPoints; pnt_id++) {
        point = mesh->GetPoint(pnt_id);
        point[0] += settings.offset;
        point[1] += settings.offset;
        point[2] += settings.offset;
        point[0] *= settings.scale;
        point[1] *= settings.scale;
        point[2] *= settings.scale;

        if (point[settings.xAxis] >= 0 && point[settings.yAxis] >= 0 && point[settings.zAxis] >= 0) {
            // Just a type conversion for the point, so do not use the axis settings here
            threadData[threadNum][chunkID].push_back({point[0], point[1], point[2]});
        }
    }
}

void COverlayHandler::mergeThreadData(OverlayChunkData threadData) const
{
    std::lock_guard<std::shared_mutex> lock(dataMutex);

    for (auto it = threadData.begin(); it != threadData.end(); ++it) {
        std::pair<OverlayChunkData::iterator, bool> ins = chunkData.insert(*it);
        if (!ins.second) {  
            // Map key already existed, so we have to merge the slice data
            OverlayData* vec1 = &(it->second);
            OverlayData* vec2 = &(ins.first->second);
            vec2->insert(vec2->end(), vec1->begin(), vec1->end());
        }
    }
}

void COverlayHandler::updateOverlayData()
{
    loadOverlayData(determineNotLoadedOverlayFiles());
}

auto COverlayHandler::getOverlayDataForView() const -> OverlayChunkDataRef
{
    OverlayChunkDataRef res;
    if (chunkData.size() == 0) { 
        return res;
    }

    auto chunks = determineChunksForView();

    for (auto chunk : chunks) {
        res[chunk] = &chunkData[chunk];
    }

    return res;
}

auto COverlayHandler::getOverlayDataForView(int zIndex) const -> OverlaySliceData
{
    OverlaySliceData res;
    if (chunkData.size() == 0) { 
        return res;
    }

    auto chunks = determineChunksForView();

    for (auto chunk : chunks) {
        for (auto point : chunkData[chunk]) {
            if (point[settings.zAxis] == zIndex) {
                res.push_back({point[settings.xAxis], point[settings.yAxis]});
            }
        }
    }

    return res;
}