#pragma once

#include <QCloseEvent>
#include <QComboBox>
#include <QMessageBox>
#include <QObject>
#include <QRect>
#include <QShortcut>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <QtWidgets>

#include "BlockingDialog.hpp"
#include "CBSpline.hpp"
#include "CXCurve.hpp"
#include "MathUtils.hpp"
#include "ui_VCMain.h"

#include "vc/core/types/VolumePkg.hpp"
#include "vc/core/util/Debug.hpp"
#include "vc/segmentation/ChainSegmentationAlgorithm.hpp"
#include "vc/segmentation/lrps/FittedCurve.hpp"

#include <thread>
#include <condition_variable>
#include <atomic>
#include <SDL2/SDL.h>
#include <cmath>
#include <unordered_map>
#include <set>

namespace ChaoVis
{

struct AnnotationStruct {
    bool anchor{false};
    // at least one point was manually changed on the slice this annotation belongs to
    bool manual{false};
    // indicates if this slice was used in a seg run as starting point / anchor; only set if
    // the slice is either an anchor or has manually changed curve points
    bool usedInRun{false};
};

struct PathChangePoint {
    int pointIndex; // index in curve
    Vec2<double> position; // X and Y on slice
    bool manuallyChanged = false; // annotation flag
};

typedef std::vector<PathChangePoint> PathChangePointVector;

struct SegmentationStruct {
    volcart::VolumePkg::Pointer fVpkg;
    std::string fSegmentationId;
    volcart::Segmentation::Pointer fSegmentation;
    // Note this might not be the original volume this segment was created with an references
    // in its meta data, as we e.g. allow loading segments created on TIFF volume to be loaded
    // for matching ZARR volume
    static volcart::Volume::Pointer currentVolume;
    volcart::Volume::Identifier fOriginalVolumeId;
    std::vector<CXCurve> fIntersections;
    std::map<int, CXCurve> fIntersectionsChanged; // manually changed curves that were not saved yet into the master cloud (key = slice index)
    CXCurve fIntersectionCurve; // current active/shown curve
    int fMaxSegIndex = 0; // index on which the segment ends
    int fMinSegIndex = 0; // index on which the segment starts
    volcart::Segmentation::PointSet fMasterCloud;
    volcart::Segmentation::PointSet fUpperPart;
    volcart::Segmentation::AnnotationSet fAnnotationCloud;
    std::vector<cv::Vec3d> fStartingPath;
    std::map<int, AnnotationStruct> fAnnotations; // decoded annotations per slice
    std::set<int> fBufferedChangedPoints; // values are in range [0..(number of points on curve - 1)] (not global cloud index, but locally to the edited curve)
    int fPathOnSliceIndex = 0;
    bool display = false;
    bool compute = false;
    bool highlighted = false;

    // Constructor
    SegmentationStruct() { // Default
    }
    SegmentationStruct(volcart::VolumePkg::Pointer vpkg, std::string segID, volcart::Segmentation::Pointer seg,
                       volcart::Volume::Pointer curVolume,
                       std::vector<CXCurve> intersections,
                       CXCurve intersectionCurve, int maxSegIndex,
                       int minSegIndex,
                       volcart::Segmentation::PointSet masterCloud,
                       volcart::Segmentation::PointSet upperPart,
                       volcart::Segmentation::AnnotationSet annotations,
                       std::vector<cv::Vec3d> startingPath,
                       int pathOnSliceIndex, bool display, bool compute)
        : fVpkg(vpkg),
          fSegmentationId(segID),
          fSegmentation(seg),
          fIntersections(intersections),
          fIntersectionCurve(intersectionCurve),
          fMaxSegIndex(maxSegIndex),
          fMinSegIndex(minSegIndex),
          fMasterCloud(masterCloud),
          fUpperPart(upperPart),
          fAnnotationCloud(annotations),
          fStartingPath(startingPath),
          fPathOnSliceIndex(pathOnSliceIndex),
          display(display),
          compute(compute) {
    }

    // Destructor to free the dynamically allocated memory
    ~SegmentationStruct() {
        fVpkg = nullptr;
        fSegmentationId.clear();
        fSegmentation = nullptr;
        fIntersections.clear();
        fIntersectionsChanged.clear();
        fMaxSegIndex = 0;
        fMinSegIndex = 0;
        fMasterCloud.clear();
        fUpperPart.clear();
        fAnnotationCloud.clear();
        fAnnotations.clear();
        fStartingPath.clear();
        fPathOnSliceIndex = 0;
        display = false;
        compute = false;
    }

    inline SegmentationStruct(volcart::VolumePkg::Pointer vpkg, std::string segID, int pathOnSliceIndex)
                                                        : fVpkg(vpkg) {
        fSegmentationId = segID;
        SetPathOnSliceIndex(pathOnSliceIndex);

        // reset point cloud
        ResetPointCloud();

        // Activate requested segmentation
        fSegmentation = fVpkg->segmentation(fSegmentationId);

        // load master point cloud
        if (fSegmentation->hasPointSet()) {
            fMasterCloud = fSegmentation->getPointSet();

            // load annotations
            if (fSegmentation->hasAnnotations()) {
                fAnnotationCloud = fSegmentation->getAnnotationSet();

                if (fAnnotationCloud.empty()) {
                    // Loading error
                    QMessageBox::warning(nullptr, QObject::tr("Invalid annotation file"), QObject::tr("Could not read the *.vcano annotation file referenced in meta.json for segment %1!\n\n"
                        "Either locate the missing file or remove the reference from meta.json in your segment folder.").arg(QString::fromStdString(fSegmentationId)));
                } else {
                    if (fMasterCloud.size() != fAnnotationCloud.size()) {
                        // Size mismatch
                        QMessageBox::information(nullptr, QObject::tr("Size mismatch"), QObject::tr("The size of the point cloud and the annotations for %1 do not match!\n\n"
                            "Perhaps the *.vcps file was changed without adjusting the *.vcano annotation file?\n\nThe application will now extend the *.vcano file to match the *.vcps file.").arg(QString::fromStdString(fSegmentationId)));

                        AlignAnnotationCloudWithPointCloud();

                        if (fMasterCloud.size() != fAnnotationCloud.size()) {
                            // If there still is a mismatch, raise an error to the user
                            QMessageBox::critical(nullptr, QObject::tr("Size mismatch"), QObject::tr("Size mismatch between point cloud and annotation cloud could not be resolved! Continue at your own risk!"));
                        }
                    }
                }
            } else {
                // create and store annotation set if not present
                fAnnotationCloud = CreateInitialAnnotationSet(fMasterCloud[0][2], fMasterCloud.height(), fMasterCloud.width());
                fSegmentation->setAnnotationSet(fAnnotationCloud);
            }
        } else {
            fMasterCloud.reset();
            fAnnotationCloud.reset();
        }

        if (fSegmentation->hasVolumeID()) {
            fOriginalVolumeId = fSegmentation->getVolumeID();
        }

        SetUpCurves();
        SetUpAnnotations();
    }

    inline void SetPathOnSliceIndex(int nPathOnSliceIndex) {
        fPathOnSliceIndex = nPathOnSliceIndex;
    }

    // Reset point cloud
    inline void ResetPointCloud(void)
    {
        fMasterCloud.reset();
        fUpperPart.reset();
        fStartingPath.clear();
        fIntersections.clear();
        fIntersectionsChanged.clear();
        CXCurve emptyCurve;
        fIntersectionCurve = emptyCurve;
        fAnnotationCloud.reset();
        fAnnotations.clear();
    }

    inline void SplitCloud(void)
    {
        // Convert volume z-index to PointSet index
        auto pathIndex = fPathOnSliceIndex - fMinSegIndex;

        if (fMasterCloud.empty() || fPathOnSliceIndex < fMinSegIndex || fPathOnSliceIndex > fMaxSegIndex) {
            fStartingPath = std::vector<cv::Vec3d>();
            return;
        }

        // Upper, "immutable" part
        if (fPathOnSliceIndex > fMinSegIndex) {
            fUpperPart = fMasterCloud.copyRows(0, pathIndex);
        } else {
            fUpperPart = volcart::OrderedPointSet<cv::Vec3d>(fMasterCloud.width());
        }

        // Lower part, the starting path
        fStartingPath = fMasterCloud.getRow(pathIndex);

        // Remove silly -1 points if they exist
        fStartingPath.erase(
            std::remove_if(
                std::begin(fStartingPath), std::end(fStartingPath),
                [](auto e) { return e[2] == -1; }),
            std::end(fStartingPath));

        // Make sure the sizes match now
        if (fStartingPath.size() != fMasterCloud.width()) {
            CleanupSegmentation();
            return;
        }
    }

    inline void CleanupSegmentation(void)
    {
        SetUpCurves();
        SetUpAnnotations();
        SetCurrentCurve(fPathOnSliceIndex);
    }

    // Get the curves for all the slices
    inline void SetUpCurves(void)
    {
        if (fVpkg == nullptr || fMasterCloud.empty()) {
            return;
        }
        fIntersections.clear();
        int minIndex, maxIndex;
        if (fMasterCloud.empty()) {
            minIndex = maxIndex = fPathOnSliceIndex;
        } else {
            minIndex = static_cast<int>(floor(fMasterCloud[0][2]));
            maxIndex = static_cast<int>(fMasterCloud.getRow(fMasterCloud.height()-1)[fMasterCloud.width()-1][2]);
        }

        fMinSegIndex = minIndex;
        fMaxSegIndex = maxIndex;

        // assign rows of particles to the curves
        for (size_t i = 0; i < fMasterCloud.height(); ++i) {
            CXCurve aCurve;
            for (size_t j = 0; j < fMasterCloud.width(); ++j) {
                int pointIndex = j + (i * fMasterCloud.width());
                aCurve.SetSliceIndex(
                    static_cast<int>(floor(fMasterCloud[pointIndex][2])));
                aCurve.InsertPoint(Vec2<double>(
                    fMasterCloud[pointIndex][0], fMasterCloud[pointIndex][1]));
            }
            fIntersections.push_back(aCurve);
        }
    }

    // Get the annotations for all the slices
    inline void SetUpAnnotations(void)
    {
        if (fVpkg == nullptr || fMasterCloud.empty() || fAnnotationCloud.empty()) {
            return;
        }

        fAnnotations.clear();
        for (size_t i = 0; i < fAnnotationCloud.height(); ++i) {
            AnnotationStruct an;
            int pointIndex;

            for (size_t j = 0; j < fAnnotationCloud.width(); ++j) {
                pointIndex = j + (i * fAnnotationCloud.width());

                if (std::get<long>(fAnnotationCloud[pointIndex][volcart::Segmentation::ANO_EL_FLAGS]) & volcart::Segmentation::ANO_ANCHOR)
                    an.anchor = true;

                if (std::get<long>(fAnnotationCloud[pointIndex][volcart::Segmentation::ANO_EL_FLAGS]) & volcart::Segmentation::ANO_MANUAL)
                    an.manual = true;

                if (std::get<long>(fAnnotationCloud[pointIndex][volcart::Segmentation::ANO_EL_FLAGS]) & volcart::Segmentation::ANO_USED_IN_RUN)
                    an.usedInRun = true;
            }

            fAnnotations[std::get<long>(fAnnotationCloud[pointIndex][volcart::Segmentation::ANO_EL_SLICE])] = an;
        }
    }

    // Set the current curve
    inline void SetCurrentCurve(int nCurrentSliceIndex)
    {
        SetPathOnSliceIndex(nCurrentSliceIndex);
        int curveIndex = nCurrentSliceIndex - fMinSegIndex;
        if (curveIndex >= 0 &&
            curveIndex < static_cast<int>(fIntersections.size()) &&
            fIntersections.size() != 0) {

            // If we have a buffered changed curve, use that one.
            // Note: The map of changed intersections uses the slice number as key,
            // where as the intersections vector needs to be accessed by the curveIndex (offset)
            auto it = fIntersectionsChanged.find(fPathOnSliceIndex);
            if (it != fIntersectionsChanged.end())
                fIntersectionCurve = it->second;
            else
                fIntersectionCurve = fIntersections[curveIndex];
        } else {
            CXCurve emptyCurve;
            fIntersectionCurve = emptyCurve;
        }
    }

    inline bool HasChangedCurves()
    {
        return (fIntersectionsChanged.empty() == false || fBufferedChangedPoints.empty() == false);
    }

    inline void ForgetChangedCurves()
    {
        fIntersectionsChanged.clear();
        fBufferedChangedPoints.clear();
    }

    inline void UpdateChangedCurvePoints(int nSliceIndex, PathChangePointVector changes)
    {
        auto it = fIntersectionsChanged.find(nSliceIndex);
        if (it != fIntersectionsChanged.end()) {
            for (auto point : changes) {
                it->second.SetPoint(point.pointIndex, point.position);
            }
        }
    }

    inline void MergePointSetIntoPointCloud(const volcart::Segmentation::PointSet ps)
    {
        if (ps.empty()) {
            return;
        }

        // Ensure that everything matches
        if (fMasterCloud.width() != ps.width() || fMasterCloud.width() != fAnnotationCloud.width()) {
            std::cout << "Error: Width mismatch during cloud merging" << std::endl;
            return;
        }
        if (fMasterCloud.height() != fAnnotationCloud.height()) {
            std::cout << "Error: Height mismatch during cloud merging" << std::endl;
            return;
        }

        // Handle point cloud logic
        int i;
        // Indicates whether the size increase is at the front (true value) = incoming point set contains lower index
        // values then what we have or at the back (false value)
        bool frontGrowth = false;

        for (i = 0; i < fMasterCloud.height(); i++) {
            auto masterRowI = fMasterCloud.getRow(i);
            if (ps[0][2] <= masterRowI[0][2]){
                // We found the entry where the 3rd vector component (= index 2 = which means the slice index)
                // of the new point set matches the value in the existing row of our master point cloud
                // => starting point for merge

                if (ps[0][2] < masterRowI[0][2]) {
                    frontGrowth = true;
                }

                break;
            }
        }

        // Copy everything below the index for merge start that we just determined (copyRows will not return back the
        // the row of "i", so no duplicates with our to-be merged point set). If "i" reached the end of our point cloud
        // above, then there are no duplicates and will simply continue with the append below.
        fUpperPart = fMasterCloud.copyRows(0, i);
        fUpperPart.append(ps);

        // Check if remaining rows already exist in fMasterCloud behind the new point set
        for(; i < fMasterCloud.height(); i++) {
            auto masterRowI = fMasterCloud.getRow(i);
            if (ps[ps.size() - 1][2] < masterRowI[fUpperPart.width()-1][2]) {
                break;
            }
        }

        // Add the remaining rows (if there are any left; potentially all are left if the input
        // points all have lower slice index values than our existing master cloud contained so far)
        if (i < fMasterCloud.height()) {
            fUpperPart.append(fMasterCloud.copyRows(i, fMasterCloud.height()));
        }

        int sizeDelta = fUpperPart.height() - fMasterCloud.height();
        // volcart::debug::PrintPointCloud(fMasterCloud, "Before Copy", true);
        fMasterCloud = fUpperPart;
        // volcart::debug::PrintPointCloud(fMasterCloud, "After Copy", true);

        // Handle annotation cloud logic

        // Check if size changed (some merges simply overwrite an existing point cloud row)
        if (fMasterCloud.size() != fAnnotationCloud.size()) {
            volcart::Segmentation::AnnotationSet fUpperAnnotations(fAnnotationCloud.width());
            const AnnotationStruct defaultAnnotation;
            long defaultAnnotationFlags = 0;
            if (defaultAnnotation.anchor) {
                defaultAnnotationFlags |= volcart::Segmentation::ANO_ANCHOR;
            }
            if (defaultAnnotation.manual) {
                defaultAnnotationFlags |= volcart::Segmentation::ANO_MANUAL;
            }
            if (defaultAnnotation.usedInRun) {
                defaultAnnotationFlags |= volcart::Segmentation::ANO_USED_IN_RUN;
            }

            // Create an initial annotation point set that matches the dimensions of the input "ps"
            // of this method minus one row (since compared to the master point set, we want to retain
            // the existing row that e.g. the segmentation was started with to not loose its flags).
            volcart::Segmentation::AnnotationSet as(fAnnotationCloud.width());
            std::vector<volcart::Segmentation::Annotation> annotations;
            double initialPos = 0;

            for (int ia = 0; ia < sizeDelta; ia++) {
                annotations.clear();
                for (int ja = 0; ja < ps.width(); ja++) {
                    // We have no annotation info for the new points, so just create initial entries
                    long sliceIndex = frontGrowth ? ps[0][2] + ia : std::get<long>(fAnnotationCloud[fAnnotationCloud.size() - 1][volcart::Segmentation::ANO_EL_SLICE]) + 1 + ia;
                    annotations.emplace_back(volcart::Segmentation::Annotation((long)sliceIndex, defaultAnnotationFlags, initialPos, initialPos));
                }
                as.pushRow(annotations);
            }

            fUpperAnnotations = fAnnotationCloud.copyRows(0, (frontGrowth ? 0 : fAnnotationCloud.height()));
            fUpperAnnotations.append(as);
            fUpperAnnotations.append(fAnnotationCloud.copyRows((frontGrowth ? 0 : fAnnotationCloud.height()), fAnnotationCloud.height()));

            fAnnotationCloud = fUpperAnnotations;
        }

        if (fMasterCloud.height() != fAnnotationCloud.height()) {
            // volcart::debug::PrintPointCloud(fMasterCloud, "Master Cloud");
            // volcart::debug::PrintPointCloud(fAnnotationCloud, "Annotation Cloud");
            std::cout << "Error: Height mismatch after cloud merging" << std::endl;
            return;
        }
    }

    inline void MergeChangedCurveIntoPointCloud(int sliceIndex)
    {
        // Check if we have a buffered changed curve for this index. If not exit.
        auto it = fIntersectionsChanged.find(sliceIndex);
        if (it == fIntersectionsChanged.end())
            return;

        volcart::Segmentation::PointSet ps(fMasterCloud.width());
        cv::Vec3d tempPt;
        std::vector<cv::Vec3d> row;
        for (size_t i = 0; i < it->second.GetPointsNum(); ++i) {
            tempPt[0] = it->second.GetPoint(i)[0];
            tempPt[1] = it->second.GetPoint(i)[1];
            tempPt[2] = it->second.GetSliceIndex();
            row.push_back(tempPt);
        }

        // Resample points so they are evenly spaced
        volcart::segmentation::FittedCurve evenlyStartingCurve(row, sliceIndex);
        row = evenlyStartingCurve.evenlySpacePoints();

        ps.pushRow(row);
        MergePointSetIntoPointCloud(ps);
    }

    inline volcart::Segmentation::AnnotationSet CreateInitialAnnotationSet(int startSlice, int height, int width)
    {
        volcart::Segmentation::AnnotationSet as(width);
        const AnnotationStruct defaultAnnotation;
        long defaultAnnotationFlags = 0;
        if (defaultAnnotation.anchor) {
            defaultAnnotationFlags |= volcart::Segmentation::ANO_ANCHOR;
        }
        if (defaultAnnotation.manual) {
            defaultAnnotationFlags |= volcart::Segmentation::ANO_MANUAL;
        }
        if (defaultAnnotation.usedInRun) {
            defaultAnnotationFlags |= volcart::Segmentation::ANO_USED_IN_RUN;
        }

        std::vector<volcart::Segmentation::Annotation> annotations;
        double initialPos = 0;
        for (int i = 0; i < height; i++) {
            annotations.clear();
            for (int j = 0; j < width; j++) {
                // We have no annotation info for the new points, so just create initial entries
                annotations.emplace_back(volcart::Segmentation::Annotation((long)(startSlice + i), defaultAnnotationFlags, initialPos, initialPos));
            }
            as.pushRow(annotations);
        }

        return as;
    }

    // Align the size of the annotation cloud to the point cloud to ensure they have the same dimensions again.
    // Mismatches can happen if the point cloud was changed outside of VC in a tool that does not handle annotations.
    // By ensuring we have the same size, we can still work with annotations in VC, although for the new point rows
    // added outside VC, we of course do only have default annotation values.
    inline void AlignAnnotationCloudWithPointCloud()
    {
        if (fMasterCloud.size() != fAnnotationCloud.size()) {
            volcart::Segmentation::AnnotationSet newCloud(fAnnotationCloud.width());

            int delta = std::abs(fMasterCloud[0][2] - std::get<long>(fAnnotationCloud[0][volcart::Segmentation::ANO_EL_SLICE]));
            // Check if we need to add rows at the start
            if (delta > 0) {
                newCloud.append(CreateInitialAnnotationSet(fMasterCloud[0][2], delta, fAnnotationCloud.width()));
            }

            newCloud.append(fAnnotationCloud);

            // Check if we need to add rows at the end
            delta = std::abs(fMasterCloud[fMasterCloud.size() - 1][2] - std::get<long>(fAnnotationCloud[fAnnotationCloud.size() - 1][volcart::Segmentation::ANO_EL_SLICE]));
            if (delta > 0) {
                newCloud.append(CreateInitialAnnotationSet(std::get<long>(fAnnotationCloud[fAnnotationCloud.size() - 1][volcart::Segmentation::ANO_EL_SLICE]) + 1, delta, fAnnotationCloud.width()));
            }

            fAnnotationCloud = newCloud;
        }
    }

    inline void SetAnnotationAnchor(int sliceIndex, bool anchor)
    {
        // Calculate index via master point cloud
        int pointIndex = GetAnnotationIndexForSliceIndex(sliceIndex);
        if (pointIndex == -1) {
            return;
        }

        for(int i = pointIndex; i < (pointIndex + fAnnotationCloud.width()); i++) {
            if (anchor) {
                std::get<long>(fAnnotationCloud[i][volcart::Segmentation::ANO_EL_FLAGS]) |= volcart::Segmentation::ANO_ANCHOR;
            } else {
                std::get<long>(fAnnotationCloud[i][volcart::Segmentation::ANO_EL_FLAGS]) &= ~volcart::Segmentation::ANO_ANCHOR;
            }
        }

        auto it = fAnnotations.find(sliceIndex);
        if (it != fAnnotations.end()) {
            // Update existing entry
            it->second.anchor = anchor;
        } else {
            // Create new entry
            AnnotationStruct an;
            an.anchor = anchor;
            fAnnotations[sliceIndex] = an;
        }
    }

    inline bool IsSliceAnAnchor(int sliceIndex) {
        return fAnnotations[sliceIndex].anchor;
    }

    inline void AddPointsToManualBuffer(std::set<int> pointIndexes)
    {
        // We need to buffer the points that we potentially have to store as "manually changed" in
        // annotations, but we cannot directly update the cloud, since the manual changes might be
        // discarded, e.g. by leaving the segmentation tool. Only once they are "confirmed" by
        // being used in a segmentation run or explicitely saved, can we update the annotation cloud.
        fBufferedChangedPoints.insert(pointIndexes.begin(), pointIndexes.end());
    }

    inline void RemovePointsFromManualBuffer(std::set<int> pointIndexes)
    {
        for (auto index : pointIndexes) {
            auto it = std::find(fBufferedChangedPoints.begin(), fBufferedChangedPoints.end(), index);
            if (it !=fBufferedChangedPoints.end()) {
                fBufferedChangedPoints.erase(std::find(fBufferedChangedPoints.begin(), fBufferedChangedPoints.end(), index));
            }
        }
    }

    inline int GetPointIndexForSliceIndex(int sliceIndex)
    {
        // Determine the first point index from the master cloud that belongs to the provided
        // slice index.
        for (int i = 0; i < fMasterCloud.height(); i++) {
            if (sliceIndex == fMasterCloud[i * fMasterCloud.width()][2]){
                return i * fMasterCloud.width();
            }
        }

        return -1;
    }

    inline int GetAnnotationIndexForSliceIndex(int sliceIndex)
    {
        // Determine the first point index from the annotation cloud that belongs to the provided
        // slice index.
        for (int i = 0; i < fAnnotationCloud.height(); i++) {
            if (sliceIndex == std::get<long>(fAnnotationCloud[i * fAnnotationCloud.width()][volcart::Segmentation::ANO_EL_SLICE])) {
                return i * fAnnotationCloud.width();
            }
        }

        return -1;
    }

    // Set annotation as "manually changed" if we have buffered curve point changes
    inline void SetAnnotationManualPoints(int sliceIndex)
    {
        auto pointIndex = GetAnnotationIndexForSliceIndex(sliceIndex);
        if (pointIndex == -1) {
            return;
        }

        if (fBufferedChangedPoints.size() > 0) {
            for (auto index : fBufferedChangedPoints) {
                std::get<long>(fAnnotationCloud[pointIndex + index][volcart::Segmentation::ANO_EL_FLAGS]) |= volcart::Segmentation::ANO_MANUAL;
            }

            auto it = fAnnotations.find(sliceIndex);
            if (it != fAnnotations.end()) {
                it->second.manual = true;
            }

            fBufferedChangedPoints.clear();
        }
    }

    // Set annotation for "used in run"
    inline void SetAnnotationUsedInRun(int sliceIndex, bool used)
    {
        // Calculate index via master point cloud
        int pointIndex = GetAnnotationIndexForSliceIndex(sliceIndex);
        if (pointIndex == -1) {
            return;
        }

        for(int i = pointIndex; i < (pointIndex + fAnnotationCloud.width()); i++) {
            if (used) {
                std::get<long>(fAnnotationCloud[i][volcart::Segmentation::ANO_EL_FLAGS]) |= volcart::Segmentation::ANO_USED_IN_RUN;
            } else {
                std::get<long>(fAnnotationCloud[i][volcart::Segmentation::ANO_EL_FLAGS]) &= ~volcart::Segmentation::ANO_USED_IN_RUN;
            }
        }

        auto it = fAnnotations.find(sliceIndex);
        if (it != fAnnotations.end()) {
            // Update existing entry
            it->second.usedInRun = used;
        } else {
            // Create new entry
            AnnotationStruct an;
            an.usedInRun = used;
            fAnnotations[sliceIndex] = an;
        }
    }

    // Set the annotation for the original position of each point as output by the segmentation algorithm
    inline void SetAnnotationOriginalPos(volcart::Segmentation::PointSet ps)
    {
        for (int i = 0; i < ps.height(); i++) {
            auto psRow = ps.getRow(i);

            auto pointIndex = GetAnnotationIndexForSliceIndex(psRow[0][2]);

            for(int j = 0; j < ps.width(); j++) {
                fAnnotationCloud[pointIndex + j][volcart::Segmentation::ANO_EL_POS_X] = psRow[j][0];
                fAnnotationCloud[pointIndex + j][volcart::Segmentation::ANO_EL_POS_Y] = psRow[j][1];
            }
        }
    }

    // Reset annotations for slices
    inline void ResetAnnotations(int startIndex, int endIndex)
    {
        // Calculate index via master point cloud
        int startPointIndex = GetAnnotationIndexForSliceIndex(startIndex);
        if (startPointIndex == -1) {
            return;
        }

        auto directionUp = endIndex > startIndex;
        int endPointIndex;

        if (directionUp) {
            endPointIndex = startPointIndex + ((endIndex - startIndex + 1) * fAnnotationCloud.width() - 1);
        } else {
            // We are going downwards => start at the end of the start slice = add the width to the index
            startPointIndex += fAnnotationCloud.width() - 1;
            endPointIndex = startPointIndex - ((startIndex - endIndex + 1) * fAnnotationCloud.width() - 1);
        }

        // Note we are not blindly resetting everything, but are reversing the flags we know are no longer relevant.
        // In the future with new annotations being added, some of them might need to remain after a segmentation run.
        // In that case we might have to create more specialized logic to determine which flags to reset.
        for(int i = startPointIndex; i != endPointIndex; directionUp ? i++ : i--) {
            std::get<long>(fAnnotationCloud[i][volcart::Segmentation::ANO_EL_FLAGS]) &= ~volcart::Segmentation::ANO_ANCHOR;
            std::get<long>(fAnnotationCloud[i][volcart::Segmentation::ANO_EL_FLAGS]) &= ~volcart::Segmentation::ANO_MANUAL;
            std::get<long>(fAnnotationCloud[i][volcart::Segmentation::ANO_EL_FLAGS]) &= ~volcart::Segmentation::ANO_USED_IN_RUN;
        }
    }

    inline int FindNearestLowerAnchor(int sliceIndex)
    {
        if (!fSegmentation->hasAnnotations()) {
            return -1;
        }

        // From provided start slice go backwards until we have an anchor
        for(int i = sliceIndex - 1; i >= fMinSegIndex; i--) {
            if (fAnnotations[i].anchor) {
                return i;
            }
        }

        // No anchor found
        return -1;
    }

    inline int FindNearestHigherAnchor(int sliceIndex)
    {
        if (!fSegmentation->hasAnnotations()) {
            return -1;
        }

        // From provided start slice go forward until we have an anchor or reached the end
        for(int i = sliceIndex + 1; i < currentVolume->numSlices(); i++) {
            if (fAnnotations[i].anchor) {
                return i;
            }
        }

        // No anchor found
        return -1;
    }

    // Handle path change event
    void OnPathChanged(void)
    {
        // update current slice
        fStartingPath.clear();
        cv::Vec3d tempPt;
        for (size_t i = 0; i < fIntersectionCurve.GetPointsNum(); ++i) {
            tempPt[0] = fIntersectionCurve.GetPoint(i)[0];
            tempPt[1] = fIntersectionCurve.GetPoint(i)[1];
            tempPt[2] = fPathOnSliceIndex;
            fStartingPath.push_back(tempPt);
        }

        // Buffer the changed path, so that if we change the displayed slice we do not loose
        // the manual changes that were made to the points of the path
        fIntersectionsChanged[fPathOnSliceIndex] = fIntersectionCurve;
    }

    void EvenlySpacePoints(int sliceIndex) {
        auto points = fIntersectionCurve.GetPoints();
        std::vector<Voxel> voxels;
        for (auto pt : points) {
            voxels.push_back(cv::Vec3d(pt[0], pt[1], sliceIndex));
        }
        volcart::segmentation::FittedCurve curve(voxels, sliceIndex);

        auto evenVoxels = curve.evenlySpacePoints();
        int i = 0;
        for (auto vx : evenVoxels) {
            fIntersectionCurve.SetPoint(i, Vec2(vx[0], vx[1]));
            i++;
        }
        fIntersectionsChanged[sliceIndex] = fIntersectionCurve;
    }
};

}  // namespace ChaoVis