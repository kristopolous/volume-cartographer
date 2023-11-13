// CWindow.cpp
// Chao Du 2014 Dec
#include "CWindow.hpp"

#include <QKeySequence>
#include <QProgressBar>
#include <QSettings>
#include <opencv2/imgproc.hpp>

#include "CVolumeViewerWithCurve.hpp"
#include "UDataManipulateUtils.hpp"
#include "SettingsDialog.hpp"

#include "vc/core/types/Color.hpp"
#include "vc/core/types/Exceptions.hpp"
#include "vc/core/util/Iteration.hpp"
#include "vc/core/util/Logging.hpp"
#include "vc/meshing/OrderedPointSetMesher.hpp"
#include "vc/segmentation/LocalResliceParticleSim.hpp"
#include "vc/segmentation/OpticalFlowSegmentation.hpp"

namespace vc = volcart;
namespace vcs = volcart::segmentation;
using namespace ChaoVis;
using qga = QGuiApplication;

struct PutTextParams {
    int font{cv::FONT_HERSHEY_SIMPLEX};
    double scale{1};
    int thickness{1};
    int baseline{0};
    cv::Size size;
};

auto CalculateOptimalTextParams(
    const std::string& str,
    int width,
    int height,
    int maxIters = 1000,
    double bufferTB = 0.2,
    double bufferLR = 0.15) -> PutTextParams
{

    // results
    PutTextParams p;

    // calculate the width and height minus the buffer
    auto maxW = width - static_cast<int>(std::ceil(2 * bufferLR * width));
    auto maxH = height - static_cast<int>(std::ceil(2 * bufferTB * height));
    auto minDim = std::min(maxW, maxH);
    auto dIdx = (minDim == maxH) ? 0 : 1;

    // calculate optimal thickness
    const auto x = static_cast<double>(minDim);
    auto t = 9.944e-11 * std::pow(x, 3) + -2.35505e-6 * std::pow(x, 2) +
             1.13691e-2 * x + 0.886545;
    p.thickness = std::min(1, std::max(static_cast<int>(t), 50));

    // iteratively find the correct scale
    for (const auto i : vc::range(maxIters)) {
        p.size =
            cv::getTextSize(str, p.font, p.scale, p.thickness, &p.baseline);
        if (p.size.width >= maxW or p.size.height >= maxH) {
            p.scale *= 0.95;
        } else {
            // get the size dim corresponding to our min dim
            auto minSize = (dIdx == 0) ? p.size.height : p.size.width;
            // scale up if we're great than 10% from our target width
            if (minSize < 0.9 * minDim) {
                p.scale *= 1.11;
            } else {
                break;
            }
        }
    }
    return p;
}

// Constructor
CWindow::CWindow()
    : fWindowState(EWindowState::WindowStateIdle)
    , fVpkg(nullptr)
    , fSegmentationId("")
    , fPathOnSliceIndex(0)
    , fVolumeViewerWidget(nullptr)
    , fPathListWidget(nullptr)
    , fPenTool(nullptr)
    , fSegTool(nullptr)
    , stopPrefetching(false)
    , prefetchSliceIndex(-1)
    , fSegStruct()
{

    ui.setupUi(this);
    ui.splitter->setSizes(QList<int>() << 300 << 100);
    SDL_Init(SDL_INIT_AUDIO);
    fVpkgChanged = false;

    // default parameters for segmentation method
    // REVISIT - refactor me
    fSegParams.fAlpha = 1.0 / 3.0;
    fSegParams.fBeta = 1.0 / 3.0;
    fSegParams.fDelta = 1.0 / 3.0;
    fSegParams.fK1 = 0.5;
    fSegParams.fK2 = 0.5;
    fSegParams.fIncludeMiddle = false;
    fSegParams.fNumIters = 15;
    fSegParams.fPeakDistanceWeight = 50;
    fSegParams.fWindowWidth = 5;
    fSegParams.targetIndex = 5;
    fSegParams.purge_cache = false;
    fSegParams.cache_slices = 300;
    fSegParams.smoothen_by_brightness = 180;
    fSegParams.outside_threshold = 60;
    fSegParams.optical_flow_pixel_threshold = 80;
    fSegParams.optical_flow_displacement_threshold = 10;
    fSegParams.enable_smoothen_outlier = true;
    fSegParams.enable_edge = false;
    fSegParams.edge_jump_distance = 6;
    fSegParams.edge_bounce_distance = 3;
    fSegParams.backwards_smoothnes_interpolation_window = 5;
    fSegParams.backwards_length = 25;

    // Process the raw impact range step string and convert to step vector
    QSettings settings("VC.ini", QSettings::IniFormat);
    QString impactRangeStr = settings.value("viewer/impact_range_steps", "1-20").toString();
    impactRangeStr = impactRangeStr.simplified();
    impactRangeStr.replace(" ", "");
    auto commaSplit = impactRangeStr.split(",");
    for(auto str : commaSplit) {
        if(str.contains("-")) {
            // Expand the range to distinct values
            auto dashSplit = str.split("-");
            // We need to have two split results (before and after the dash), otherwise skip
            if(dashSplit.size() == 2) {
                for(int i = dashSplit.at(0).toInt(); i <= dashSplit.at(1).toInt(); i++) {
                    impactRangeSteps.push_back(i);
                }
            }
        } else {
            impactRangeSteps.push_back(str.toInt());
        }
    }

    // create UI widgets
    CreateWidgets();

    // create menu
    CreateActions();
    CreateMenus();
    UpdateRecentVolpkgActions();
    CreateBackend();

    OpenSlice();
    UpdateView();

    // Restore geometry / sizes
    QSettings geometry;
    if (geometry.contains("mainWin/geometry")) {
        restoreGeometry(geometry.value("mainWin/geometry").toByteArray());
    }
    if (geometry.contains("mainWin/state")) {
        restoreState(geometry.value("mainWin/state").toByteArray());
    }

    // If enabled, auto open the last used volpkg
    if (settings.value("volpkg/auto_open", false).toInt() != 0) {

        QStringList files = settings.value("volpkg/recent").toStringList();

        if(files.size() > 0 && !files.at(0).isEmpty()) {
            Open(files[0]);
        }
    }
}

// Destructor
CWindow::~CWindow(void)
{
    stopPrefetching.store(true);
    cv.notify_one();  // Wake up the thread if it's waitings
    worker_thread_.quit();
    worker_thread_.wait();
    SDL_Quit();
}

// Handle key press event
void CWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        // REVISIT - should prompt warning before exit
        Close();
    } else {
        // REVISIT - dispatch key press event
    }
}

// Create widgets
void CWindow::CreateWidgets(void)
{
    // add volume viewer
    QWidget* aTabSegment = this->findChild<QWidget*>("tabSegment");
    assert(aTabSegment != nullptr);

    fVolumeViewerWidget = new CVolumeViewerWithCurve(fSegStructMap);
    connect(fVolumeViewerWidget, &CVolumeViewerWithCurve::SendSignalStatusMessageAvailable, this, &CWindow::onShowStatusMessage);
    connect(fVolumeViewerWidget, &CVolumeViewerWithCurve::SendSignalImpactRangeUp, this, &CWindow::onImpactRangeUp);
    connect(fVolumeViewerWidget, &CVolumeViewerWithCurve::SendSignalImpactRangeDown, this, &CWindow::onImpactRangeDown);

    QVBoxLayout* aWidgetLayout = new QVBoxLayout;
    aWidgetLayout->addWidget(fVolumeViewerWidget);

    aTabSegment->setLayout(aWidgetLayout);

    // pass the reference of the curve to the widget
    fVolumeViewerWidget->SetSplineCurve(fSplineCurve);
    fVolumeViewerWidget->SetIntersectionCurve(fSegStructMap[fSegmentationId].fIntersectionCurve);

    connect(
        fVolumeViewerWidget, SIGNAL(SendSignalOnNextSliceShift(int)), this,
        SLOT(OnLoadNextSliceShift(int)));
    connect(
        fVolumeViewerWidget, SIGNAL(SendSignalOnPrevSliceShift(int)), this,
        SLOT(OnLoadPrevSliceShift(int)));
    connect(
        fVolumeViewerWidget, SIGNAL(SendSignalOnLoadAnyImage(int)), this,
        SLOT(OnLoadAnySlice(int)));
    connect(
        fVolumeViewerWidget, SIGNAL(SendSignalPathChanged()), this,
        SLOT(OnPathChanged()));

    // new path button
    QPushButton* aBtnNewPath = this->findChild<QPushButton*>("btnNewPath");
    QPushButton* aBtnRemovePath =
        this->findChild<QPushButton*>("btnRemovePath");
    connect(aBtnNewPath, SIGNAL(clicked()), this, SLOT(OnNewPathClicked()));
    connect(aBtnRemovePath, SIGNAL(clicked()), this, SLOT(OnRemovePathClicked()));

    // TODO CHANGE VOLUME LOADING; FIRST CHECK FOR OTHER VOLUMES IN THE STRUCTS
    volSelect = this->findChild<QComboBox*>("volSelect");
    connect(
        volSelect, &QComboBox::currentIndexChanged, [this](const int& index) {
            vc::Volume::Pointer newVolume;
            try {
                newVolume = fVpkg->volume(volSelect->currentData().toString().toStdString());
            } catch (const std::out_of_range& e) {
                QMessageBox::warning(this, "Error", "Could not load volume.");
                return;
            }
            currentVolume = newVolume;
            OnLoadAnySlice(0);
            setDefaultWindowWidth(newVolume);
            fVolumeViewerWidget->setNumSlices(currentVolume->numSlices());
            fEdtStartIndex->setMaximum(currentVolume->numSlices());
            fEdtEndIndex->setMaximum(currentVolume->numSlices());
        });

    assignVol = this->findChild<QPushButton*>("assignVol");
    connect(assignVol, &QPushButton::clicked, [this](bool) {
        if (fSegStructMap[fSegmentationId].fSegmentation == nullptr || fSegStructMap[fSegmentationId].fSegmentation->hasVolumeID()) {
            return;
        }
        fSegStructMap[fSegmentationId].fSegmentation->setVolumeID(currentVolume->id());
        UpdateView();
    });

    // pen tool and edit tool
    fPenTool = this->findChild<QPushButton*>("btnPenTool");
    fSegTool = this->findChild<QPushButton*>("btnSegTool");
    connect(fPenTool, SIGNAL(clicked()), this, SLOT(TogglePenTool()));
    connect(fSegTool, SIGNAL(clicked()), this, SLOT(ToggleSegmentationTool()));
    fPenTool->setStyleSheet(QString("QPushButton:checked { background-color: rgb(%1, %2, %3); }")
        .arg(QGuiApplication::palette().color(QPalette::Highlight).red())
        .arg(QGuiApplication::palette().color(QPalette::Highlight).green())
        .arg(QGuiApplication::palette().color(QPalette::Highlight).blue()));
    fSegTool->setStyleSheet(QString("QPushButton:checked { background-color: rgb(%1, %2, %3); }")
        .arg(QGuiApplication::palette().color(QPalette::Highlight).red())
        .arg(QGuiApplication::palette().color(QPalette::Highlight).green())
        .arg(QGuiApplication::palette().color(QPalette::Highlight).blue()));

    fchkDisplayAll = this->findChild<QCheckBox*>("chkDisplayAll");
    fchkComputeAll = this->findChild<QCheckBox*>("chkComputeAll");
    connect(fchkDisplayAll, &QCheckBox::toggled, this, &CWindow::toggleDisplayAll);
    connect(fchkComputeAll, &QCheckBox::toggled, this, &CWindow::toggleComputeAll);

    // list of paths
    fPathListWidget = this->findChild<QTreeWidget*>("treeWidgetPaths");
    connect(fPathListWidget, SIGNAL(itemClicked(QTreeWidgetItem*, int)), this, SLOT(OnPathItemClicked(QTreeWidgetItem*, int)));
    fPathListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(fPathListWidget, &QTreeWidget::customContextMenuRequested, this, &CWindow::OnPathCustomContextMenu);

    // segmentation methods
    auto* aSegMethodsComboBox = this->findChild<QComboBox*>("cmbSegMethods");
    aSegMethodsComboBox->addItem(tr("Local Reslice Particle Simulation"));
    aSegMethodsComboBox->addItem(tr("Optical Flow Segmentation"));
    connect(
        aSegMethodsComboBox, SIGNAL(currentIndexChanged(int)), this,
        SLOT(OnChangeSegAlgo(int)));

    // ADD NEW SEGMENTATION ALGORITHM NAMES HERE
    // aSegMethodsComboBox->addItem(tr("My custom algorithm"));

    // Optical Flow Segmentation Parameters
    auto* edtOutsideThreshold = new QSpinBox();
    edtOutsideThreshold->setMinimum(0);
    edtOutsideThreshold->setMaximum(255);
    edtOutsideThreshold->setValue(60);
    auto* edtOpticalFlowPixelThreshold = new QSpinBox();
    edtOpticalFlowPixelThreshold->setMinimum(0);
    edtOpticalFlowPixelThreshold->setMaximum(255);
    edtOpticalFlowPixelThreshold->setValue(80);
    auto* edtOpticalFlowDisplacementThreshold = new QSpinBox();
    edtOpticalFlowDisplacementThreshold->setMinimum(0);
    edtOpticalFlowDisplacementThreshold->setValue(10);
    auto* edtSmoothenPixelThreshold = new QSpinBox();
    edtSmoothenPixelThreshold->setMinimum(0);
    edtSmoothenPixelThreshold->setMaximum(256);
    edtSmoothenPixelThreshold->setValue(180);
    auto* chkEnableSmoothenOutlier = new QCheckBox(tr("Smoothen Outlier Points"));
    chkEnableSmoothenOutlier->setChecked(true);
    auto* chkEnableEdgeDetection = new QCheckBox(tr("Enable Edge Detection"));
    chkEnableEdgeDetection->setChecked(false);
    auto* edtEdgeJumpDistance = new QSpinBox();
    edtEdgeJumpDistance->setMinimum(0);
    edtEdgeJumpDistance->setValue(6);
    auto* edtEdgeBounceDistance = new QSpinBox();
    edtEdgeBounceDistance->setMinimum(0);
    edtEdgeBounceDistance->setValue(3);
    auto* edtBackwardsLength = new QSpinBox();
    edtBackwardsLength->setMinimum(0);
    edtSmoothenPixelThreshold->setMaximum(1000);
    edtBackwardsLength->setValue(25);
    auto* edtBackwardsInterpolationWindow = new QSpinBox();
    edtBackwardsInterpolationWindow->setMinimum(0);
    edtBackwardsInterpolationWindow->setValue(5);
    auto* chkPurgeCache = new QCheckBox(tr("Purge Cache"));
    chkPurgeCache->setChecked(false);
    auto* edtCacheSize = new QSpinBox();
    edtCacheSize->setMinimum(-1);
    edtCacheSize->setMaximum(20000);
    edtCacheSize->setValue(300);

    connect(edtOutsideThreshold, &QSpinBox::valueChanged, [=](int v){fSegParams.outside_threshold = v;});
    connect(edtOpticalFlowPixelThreshold, &QSpinBox::valueChanged, [=](int v){fSegParams.optical_flow_pixel_threshold = v;});
    connect(edtOpticalFlowDisplacementThreshold, &QSpinBox::valueChanged, [=](int v){fSegParams.optical_flow_displacement_threshold = v;});
    connect(edtSmoothenPixelThreshold, &QSpinBox::valueChanged, [=](int v){fSegParams.smoothen_by_brightness = v;});
    connect(chkEnableSmoothenOutlier, &QCheckBox::toggled, [=](bool checked){fSegParams.enable_smoothen_outlier = checked;});
    connect(chkEnableEdgeDetection, &QCheckBox::toggled, [=](bool checked){fSegParams.enable_edge = checked;});
    connect(edtEdgeJumpDistance, &QSpinBox::valueChanged, [=](int v){fSegParams.edge_jump_distance = v;});
    connect(edtEdgeBounceDistance, &QSpinBox::valueChanged, [=](int v){fSegParams.edge_bounce_distance = v;});
    connect(edtBackwardsLength, &QSpinBox::valueChanged, [=](int v){fSegParams.backwards_length = v;});
    connect(edtBackwardsInterpolationWindow, &QSpinBox::valueChanged, [=](int v){fSegParams.backwards_smoothnes_interpolation_window = v;});
    connect(chkPurgeCache, &QCheckBox::toggled, [=](bool checked){fSegParams.purge_cache = checked;});
    connect(edtCacheSize, &QSpinBox::valueChanged, [=](int v){fSegParams.cache_slices = v;});

    auto* opticalFlowParamsContainer = new QWidget();
    auto* opticalFlowParamsLayout = new QVBoxLayout(opticalFlowParamsContainer);

    opticalFlowParamsLayout->addWidget(new QLabel(tr("Optical Flow Displacement Threshold")));
    opticalFlowParamsLayout->addWidget(edtOpticalFlowDisplacementThreshold);
    opticalFlowParamsLayout->addWidget(new QLabel(tr("Optical Flow Dark Pixel Threshold")));
    opticalFlowParamsLayout->addWidget(edtOpticalFlowPixelThreshold);
    opticalFlowParamsLayout->addWidget(new QLabel(tr("Smoothen Curve at Dark Points")));
    opticalFlowParamsLayout->addWidget(edtOutsideThreshold);
    opticalFlowParamsLayout->addWidget(new QLabel(tr("Smoothen Curve at Bright Points")));
    opticalFlowParamsLayout->addWidget(edtSmoothenPixelThreshold);
    opticalFlowParamsLayout->addWidget(chkEnableSmoothenOutlier);
    opticalFlowParamsLayout->addWidget(chkEnableEdgeDetection);
    opticalFlowParamsLayout->addWidget(new QLabel(tr("Edge Max Jump Distance")));
    opticalFlowParamsLayout->addWidget(edtEdgeJumpDistance);
    opticalFlowParamsLayout->addWidget(new QLabel(tr("Edge Bounce Distance")));
    opticalFlowParamsLayout->addWidget(edtEdgeBounceDistance);
    opticalFlowParamsLayout->addWidget(new QLabel(tr("Backwards Length")));
    opticalFlowParamsLayout->addWidget(edtBackwardsLength);
    opticalFlowParamsLayout->addWidget(new QLabel(tr("Backwards Interpolation Window")));
    opticalFlowParamsLayout->addWidget(edtBackwardsInterpolationWindow);
    opticalFlowParamsLayout->addWidget(chkPurgeCache);
    opticalFlowParamsLayout->addWidget(new QLabel(tr("Maximum Cache Size")));
    opticalFlowParamsLayout->addWidget(edtCacheSize);

    this->ui.segParamsStack->addWidget(opticalFlowParamsContainer);
    // set the default segmentation method as Optical Flow Segmentation
    aSegMethodsComboBox->setCurrentIndex(1);
    OnChangeSegAlgo(1);

    // LRPS segmentation parameters
    // all of these are contained in this->ui.lrpsParams
    fEdtAlpha = this->findChild<QLineEdit*>("edtAlphaVal");
    fEdtBeta = this->findChild<QLineEdit*>("edtBetaVal");
    fEdtDelta = this->findChild<QLineEdit*>("edtDeltaVal");
    fEdtK1 = this->findChild<QLineEdit*>("edtK1Val");
    fEdtK2 = this->findChild<QLineEdit*>("edtK2Val");
    fEdtDistanceWeight = this->findChild<QLineEdit*>("edtDistanceWeightVal");
    fEdtWindowWidth = this->findChild<QSpinBox*>("edtWindowWidthVal");
    fEdtWindowWidth->setMinimum(3);
    fEdtWindowWidth->setValue(5);
    fOptIncludeMiddle = this->findChild<QCheckBox*>("includeMiddleOpt");
    connect(
        fEdtAlpha, SIGNAL(editingFinished()), this,
        SLOT(OnEdtAlphaValChange()));
    connect(
        fEdtBeta, SIGNAL(editingFinished()), this, SLOT(OnEdtBetaValChange()));
    connect(
        fEdtDelta, SIGNAL(editingFinished()), this,
        SLOT(OnEdtDeltaValChange()));
    connect(fEdtK1, SIGNAL(editingFinished()), this, SLOT(OnEdtK1ValChange()));
    connect(fEdtK2, SIGNAL(editingFinished()), this, SLOT(OnEdtK2ValChange()));
    connect(
        fEdtDistanceWeight, SIGNAL(editingFinished()), this,
        SLOT(OnEdtDistanceWeightChange()));
    connect(
        fEdtWindowWidth, &QSpinBox::valueChanged, this,
        &CWindow::OnEdtWindowWidthChange);
    connect(
        fOptIncludeMiddle, SIGNAL(clicked(bool)), this,
        SLOT(OnOptIncludeMiddleClicked(bool)));

    fEdtStartIndex = this->findChild<QSpinBox*>("spinStartingSliceVal");
    fEdtStartIndex->setMinimum(0);
    fEdtEndIndex = this->findChild<QSpinBox*>("spinEndingSliceVal");
    connect(
        fEdtStartIndex, SIGNAL(valueChanged(int)), this,
        SLOT(OnEdtStartingSliceValChange(int)));
    connect(
        fEdtEndIndex, SIGNAL(editingFinished()), this,
        SLOT(OnEdtEndingSliceValChange()));


    // INSERT OTHER SEGMENTATION PARAMETER WIDGETS HERE
    // this->ui.segParamsStack->addWidget(new QLabel("Parameter widgets here"));

    // start segmentation button
    QPushButton* aBtnStartSeg = this->findChild<QPushButton*>("btnStartSeg");
    connect(
        aBtnStartSeg, SIGNAL(clicked()), this, SLOT(OnBtnStartSegClicked()));

    // Impact Range slider
    fLabImpactRange = this->findChild<QLabel*>("labImpactRange");
    QSlider* fEdtImpactRng = this->findChild<QSlider*>("sldImpactRange");
    // We use the slider to provide us an index into the vector of real impact values
    // => range 0..size()-1
    fEdtImpactRng->setMinimum(0);
    fEdtImpactRng->setMaximum(impactRangeSteps.size() - 1);
    // "Randomly" set the starting value to the middle of the steps
    connect(
        fEdtImpactRng, SIGNAL(valueChanged(int)), this,
        SLOT(OnEdtImpactRange(int)));
    fEdtImpactRng->setValue(impactRangeSteps.size() / 2);

    // Set up the status bar
    statusBar = this->findChild<QStatusBar*>("statusBar");

    // setup shortcuts
    slicePrev = new QShortcut(QKeySequence(Qt::Key_Left), this);
    sliceNext = new QShortcut(QKeySequence(Qt::Key_Right), this);
    sliceZoomIn = new QShortcut(QKeySequence::ZoomIn, this);
    sliceZoomOut = new QShortcut(QKeySequence::ZoomOut, this);
    displayCurves = new QShortcut(QKeySequence(Qt::Key_Space), this);
    displayCurves_C = new QShortcut(QKeySequence(Qt::Key_C), this); // For NoMachine Segmenters
    impactDwn = new QShortcut(QKeySequence(Qt::Key_A), this);
    impactUp = new QShortcut(QKeySequence(Qt::Key_D), this);
    impactDwn_old = new QShortcut(QKeySequence(tr("[")), this);
    impactUp_old = new QShortcut(QKeySequence(tr("]")), this);
    segmentationToolShortcut = new QShortcut(QKeySequence(Qt::Key_T), this);
    penToolShortcut = new QShortcut(QKeySequence(Qt::Key_P), this);
    prev1 = new QShortcut(QKeySequence(Qt::Key_1), this);
    next1 = new QShortcut(QKeySequence(Qt::Key_2), this);
    prev2 = new QShortcut(QKeySequence(Qt::Key_3), this);
    next2 = new QShortcut(QKeySequence(Qt::Key_4), this);
    prev5 = new QShortcut(QKeySequence(Qt::Key_5), this);
    next5 = new QShortcut(QKeySequence(Qt::Key_6), this);
    prev10 = new QShortcut(QKeySequence(Qt::Key_7), this);
    next10 = new QShortcut(QKeySequence(Qt::Key_8), this);
    prev100 = new QShortcut(QKeySequence(Qt::Key_9), this);
    next100 = new QShortcut(QKeySequence(Qt::Key_0), this);
    prevSelectedId = new QShortcut(QKeySequence(Qt::Key_K), this);
    nextSelectedId = new QShortcut(QKeySequence(Qt::Key_J), this);
    goToSlice = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_G), this);
    scanRangeUp = new QShortcut(QKeySequence(Qt::Key_E), this);
    scanRangeDown = new QShortcut(QKeySequence(Qt::Key_Q), this);
    returnToEditSlice = new QShortcut(QKeySequence(Qt::Key_F), this);

    connect(
        slicePrev, &QShortcut::activated, fVolumeViewerWidget,
        &CVolumeViewerWithCurve::OnPrevClicked);
    connect(
        sliceNext, &QShortcut::activated, fVolumeViewerWidget,
        &CVolumeViewerWithCurve::OnNextClicked);
    connect(
        sliceZoomIn, &QShortcut::activated, fVolumeViewerWidget,
        &CVolumeViewerWithCurve::OnZoomInClicked);
    connect(
        sliceZoomOut, &QShortcut::activated, fVolumeViewerWidget,
        &CVolumeViewerWithCurve::OnZoomOutClicked);
    connect(
        displayCurves, &QShortcut::activated, fVolumeViewerWidget,
        &CVolumeViewerWithCurve::toggleShowCurveBox);
    connect(
        displayCurves_C, &QShortcut::activated, fVolumeViewerWidget,
        &CVolumeViewerWithCurve::toggleShowCurveBox);
    connect(impactUp, &QShortcut::activated, this, &CWindow::onImpactRangeUp);
    connect(impactDwn, &QShortcut::activated, this, &CWindow::onImpactRangeDown);
    connect(impactUp_old, &QShortcut::activated, this, &CWindow::onImpactRangeUp);
    connect(impactDwn_old, &QShortcut::activated, this, &CWindow::onImpactRangeDown);
    connect(segmentationToolShortcut, &QShortcut::activated, this, &CWindow::ActivateSegmentationTool);
    connect(penToolShortcut, &QShortcut::activated, this, &CWindow::ActivatePenTool);
    connect(next1, &QShortcut::activated, [this]() {
        int shift = 1;
        OnLoadNextSliceShift(shift);
    });
    connect(prev1, &QShortcut::activated, [this]() {
        int shift = 1;
        OnLoadPrevSliceShift(shift);
    });
    connect(next2, &QShortcut::activated, [this]() {
        int shift = 2;
        OnLoadNextSliceShift(shift);
    });
    connect(prev2, &QShortcut::activated, [this]() {
        int shift = 2;
        OnLoadPrevSliceShift(shift);
    });
    connect(next5, &QShortcut::activated, [this]() {
        int shift = 5;
        OnLoadNextSliceShift(shift);
    });
    connect(prev5, &QShortcut::activated, [this]() {
        int shift = 5;
        OnLoadPrevSliceShift(shift);
    });
    connect(next10, &QShortcut::activated, [this]() {
        int shift = 10;
        OnLoadNextSliceShift(shift);
    });
    connect(prev10, &QShortcut::activated, [this]() {
        int shift = 10;
        OnLoadPrevSliceShift(shift);
    });
    connect(next100, &QShortcut::activated, [this]() {
        int shift = 100;
        OnLoadNextSliceShift(shift);
    });
    connect(prev100, &QShortcut::activated, [this]() {
        int shift = 100;
        OnLoadPrevSliceShift(shift);
    });
    connect(prevSelectedId, &QShortcut::activated, this, &CWindow::PreviousSelectedId);
    connect(nextSelectedId, &QShortcut::activated, this, &CWindow::NextSelectedId);
    connect(goToSlice, &QShortcut::activated, this, &CWindow::ShowGoToSliceDlg);
    connect(scanRangeUp, &QShortcut::activated, this, &CWindow::ScanRangeUp);
    connect(scanRangeDown, &QShortcut::activated, this, &CWindow::ScanRangeDown);
    connect(returnToEditSlice, &QShortcut::activated, this, &CWindow::ReturnToEditSlice);
}

// Create menus
void CWindow::CreateMenus(void)
{
    // "Recent Volpkg" menu
    fRecentVolpkgMenu = new QMenu(tr("Open &recent volpkg"), this);
    fRecentVolpkgMenu->setEnabled(false);
    for (auto& action : fOpenRecentVolpkg)
    {
        fRecentVolpkgMenu->addAction(action);
    }

    fFileMenu = new QMenu(tr("&File"), this);
    fFileMenu->addAction(fOpenVolAct);
    fFileMenu->addMenu(fRecentVolpkgMenu);
    fFileMenu->addSeparator();
    fFileMenu->addAction(fSavePointCloudAct);
    fFileMenu->addSeparator();
    fFileMenu->addAction(fSettingsAct);
    fFileMenu->addSeparator();
    fFileMenu->addAction(fExitAct);

    fHelpMenu = new QMenu(tr("&Help"), this);
    fHelpMenu->addAction(fKeybinds);
    fFileMenu->addSeparator();

    QSettings settingsJump("VC.ini", QSettings::IniFormat);
    if(settingsJump.value("internal/debug", 0).toInt() == 1) {
        fHelpMenu->addAction(fPrintDebugInfo);
        fFileMenu->addSeparator();
    }

    fHelpMenu->addAction(fAboutAct);

    menuBar()->addMenu(fFileMenu);
    menuBar()->addMenu(fHelpMenu);
}

// Create actions
void CWindow::CreateActions(void)
{
    fOpenVolAct = new QAction(tr("&Open volpkg..."), this);
    connect(fOpenVolAct, SIGNAL(triggered()), this, SLOT(Open()));
    fOpenVolAct->setShortcut(QKeySequence::Open);

    for(auto& action : fOpenRecentVolpkg)
    {
        action = new QAction(this);
        action->setVisible(false);
        connect(action, &QAction::triggered, this, &CWindow::OpenRecent);
    }

    fSavePointCloudAct = new QAction(tr("&Save volpkg..."), this);
    connect(
        fSavePointCloudAct, SIGNAL(triggered()), this, SLOT(SavePointCloud()));
    fSavePointCloudAct->setShortcut(QKeySequence::Save);

    fSettingsAct = new QAction(tr("Settings"), this);
    connect(fSettingsAct, SIGNAL(triggered()), this, SLOT(ShowSettings()));

    fExitAct = new QAction(tr("E&xit..."), this);
    connect(fExitAct, SIGNAL(triggered()), this, SLOT(Close()));

    fKeybinds = new QAction(tr("&Keybinds"), this);
    connect(fKeybinds, SIGNAL(triggered()), this, SLOT(Keybindings()));

    fAboutAct = new QAction(tr("&About..."), this);
    connect(fAboutAct, SIGNAL(triggered()), this, SLOT(About()));

    fPrintDebugInfo = new QAction  (tr("Debug info"), this);
    connect(fPrintDebugInfo, SIGNAL(triggered()), this, SLOT(PrintDebugInfo()));
}

void CWindow::CreateBackend()
{
    // Setup backend runner
    auto* worker = new VolPkgBackend();
    worker->moveToThread(&worker_thread_);
    connect(&worker_thread_, &QThread::finished, worker, &QObject::deleteLater);
    connect(
        this, &CWindow::submitSegmentation, worker,
        &VolPkgBackend::startSegmentation);
    connect(
        worker, &VolPkgBackend::segmentationFinished, this,
        &CWindow::onSegmentationFinished);
    connect(
        worker, &VolPkgBackend::segmentationFailed, this,
        &CWindow::onSegmentationFailed);
    connect(worker, &VolPkgBackend::progressUpdated, [=](size_t p) {
        progress_ = p;
    });
    worker_thread_.start();

    // Setup progress dialog
    auto layout = new QVBoxLayout();
    worker_progress_.setLayout(layout);
    progressLabel_ = new QLabel("Segmentation in progress. Please wait...");
    layout->addWidget(progressLabel_);
    progressBar_ = new QProgressBar();
    layout->addWidget(progressBar_);
    progressBar_->setMinimum(0);
    connect(worker, &VolPkgBackend::segmentationStarted, [=](size_t its) {
        progressBar_->setMaximum(its);
    });

    // Update the GUI intermittently
    worker_progress_updater_.setInterval(1000);
    connect(&worker_progress_updater_, &QTimer::timeout, [=]() {
        if (progressLabel_->text() ==
            "Segmentation in progress. Please wait...") {
            progressLabel_->setText("Segmentation in progress. Please wait");
        } else {
            progressLabel_->setText(progressLabel_->text().append('.'));
        }
        progressBar_->setValue(progress_);
    });
}

void CWindow::UpdateRecentVolpkgActions()
{
    QSettings settings("VC.ini", QSettings::IniFormat);
    QStringList files = settings.value("volpkg/recent").toStringList();
    if(files.isEmpty()) {
        return;
    }

    // The automatic conversion to string list from the settings, (always?) adds an
    // empty entry at the end. Remove it if present.
    if(files.last().isEmpty()) {
        files.removeLast();
    }

    const int numRecentFiles = qMin(files.size(), static_cast<int>(MAX_RECENT_VOLPKG));

    for(int i = 0; i < numRecentFiles; ++i) {
        // Replace "&" with "&&" since otherwise they will be hidden and interpreted
        // as mnemonics
        QString fileName = QFileInfo(files[i]).fileName();
        fileName.replace("&", "&&");
        QString path = QFileInfo(files[i]).canonicalPath();

        if(path == "."){
            path = tr("Directory not available!");
        } else {
            path.replace("&", "&&");
        }

        QString text = tr("&%1 | %2 (%3)").arg(i + 1).arg(fileName).arg(path);
        fOpenRecentVolpkg[i]->setText(text);
        fOpenRecentVolpkg[i]->setData(files[i]);
        fOpenRecentVolpkg[i]->setVisible(true);
    }

    for(int j = numRecentFiles; j < MAX_RECENT_VOLPKG; ++j) {
        fOpenRecentVolpkg[j]->setVisible(false);
    }

    fRecentVolpkgMenu->setEnabled(numRecentFiles > 0);
}

void CWindow::UpdateRecentVolpkgList(const QString& path)
{
    QSettings settings("VC.ini", QSettings::IniFormat);
    QStringList files = settings.value("volpkg/recent").toStringList();
    const QString pathCanonical = QFileInfo(path).absoluteFilePath();
    files.removeAll(pathCanonical);
    files.prepend(pathCanonical);

    while(files.size() > MAX_RECENT_VOLPKG) {
        files.removeLast();
    }

    settings.setValue("volpkg/recent", files);

    UpdateRecentVolpkgActions();
}

void CWindow::RemoveEntryFromRecentVolpkg(const QString& path)
{
    QSettings settings("VC.ini", QSettings::IniFormat);
    QStringList files = settings.value("volpkg/recent").toStringList();
    files.removeAll(path);
    settings.setValue("volpkg/recent", files);

    UpdateRecentVolpkgActions();
}

// Asks User to Save Data Prior to VC.app Exit
void CWindow::closeEvent(QCloseEvent* closing)
{
    if (SaveDialog() == SaveResponse::Continue) {
        closing->accept();
    } else {
        closing->ignore();
    }
    QSettings settings;
    settings.setValue("mainWin/geometry", saveGeometry());
    settings.setValue("mainWin/state", saveState());
}

void CWindow::setWidgetsEnabled(bool state)
{
    this->findChild<QGroupBox*>("grpVolManager")->setEnabled(state);
    this->findChild<QGroupBox*>("grpSeg")->setEnabled(state);
    this->findChild<QPushButton*>("btnSegTool")->setEnabled(state);
    this->findChild<QPushButton*>("btnPenTool")->setEnabled(state);
    this->findChild<QGroupBox*>("grpEditing")->setEnabled(state);
    fVolumeViewerWidget->setButtonsEnabled(state);
}

bool CWindow::InitializeVolumePkg(const std::string& nVpkgPath)
{
    fVpkg = nullptr;

    try {
        fVpkg = vc::VolumePkg::New(nVpkgPath);
    } catch (const std::exception& e) {
        vc::Logger()->error("Failed to initialize volpkg: {}", e.what());
    }

    fVpkgChanged = false;

    if (fVpkg == nullptr) {
        vc::Logger()->error("Cannot open .volpkg: {}", nVpkgPath);
        QMessageBox::warning(
            this, "Error",
            "Volume package failed to load. Package might be corrupt.");
        return false;
    }
    return true;
}

void CWindow::setDefaultWindowWidth(vc::Volume::Pointer volume)
{
    // Update window width based on selected volume
    auto winWidth = std::ceil(fVpkg->materialThickness() / volume->voxelSize());
    fEdtWindowWidth->setValue(static_cast<int>(winWidth));
}

CWindow::SaveResponse CWindow::SaveDialog(void)
{
    // Return if nothing has changed
    if (not fVpkgChanged) {
        return SaveResponse::Continue;
    }

    const auto response = QMessageBox::question(
        this, "Save changes?",
        tr("Changes will be lost! Save volume package before continuing?\n"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    switch (response) {
        case QMessageBox::Save:
            SavePointCloud();
            return SaveResponse::Continue;
        case QMessageBox::Discard:
            fVpkgChanged = false;
            return SaveResponse::Continue;
        default:
            return SaveResponse::Cancelled;
    }
    return SaveResponse::Cancelled;
}

// Update the widgets
void CWindow::UpdateView(void)
{
    if (fVpkg == nullptr) {
        setWidgetsEnabled(false);  // Disable Widgets for User
        this->findChild<QLabel*>("lblVpkgName")
            ->setText("[ No Volume Package Loaded ]");
        return;
    }

    setWidgetsEnabled(true);  // Enable Widgets for User

    // show volume package name
    this->findChild<QLabel*>("lblVpkgName")
        ->setText(QString(fVpkg->name().c_str()));

    // set widget accessibility properly based on the states: is drawing? is
    // editing?
    fEdtAlpha->setText(QString("%1").arg(fSegParams.fAlpha));
    fEdtBeta->setText(QString("%1").arg(fSegParams.fBeta));
    fEdtDelta->setText(QString("%1").arg(fSegParams.fDelta));
    fEdtK1->setText(QString("%1").arg(fSegParams.fK1));
    fEdtK2->setText(QString("%1").arg(fSegParams.fK2));
    fEdtDistanceWeight->setText(
        QString("%1").arg(fSegParams.fPeakDistanceWeight));
    fEdtWindowWidth->setValue(fSegParams.fWindowWidth);

    // Set / calculate start and end index
    fEdtStartIndex->setValue(fSliceIndexToolStart);
    if (fSliceIndexToolStart + fEndTargetOffset >= currentVolume->numSlices()) {
        fEdtEndIndex->setValue(currentVolume->numSlices() - 1);
    }
    else if (fSliceIndexToolStart + fEndTargetOffset < 0) {
        fEdtEndIndex->setValue(0);
    }
    else {
        fEdtEndIndex->setValue(fSliceIndexToolStart + fEndTargetOffset);
    }

    // Logic to enable/disable segmentation and pen tools. TODO add logic to check proper segmentations
    bool availableSegments = false;
    bool availableNewSegments = false;
    for (auto& seg : fSegStructMap) {
        auto& segStruct = seg.second;
        if (!segStruct.display && !segStruct.compute) {
            continue;
        }
        // segments with already existing line
        availableSegments = availableSegments || segStruct.fIntersectionCurve.GetPointsNum() > 0;
        // completely empty segments, for drawing curve
        availableNewSegments = availableNewSegments || (!segStruct.fSegmentationId.empty() && segStruct.fMasterCloud.empty());
    }
    fSegTool->setEnabled(!availableNewSegments && availableSegments);
    fPenTool->setEnabled(availableNewSegments);

    volSelect->setEnabled(can_change_volume_());
    assignVol->setEnabled(can_change_volume_());

    // REVISIT - these two states should be mutually exclusive, we guarantee
    // this when we toggle the button, BUGGY!
    if (fWindowState == EWindowState::WindowStateIdle) {
        fVolumeViewerWidget->SetViewState(
            CVolumeViewerWithCurve::EViewState::ViewStateIdle);
        this->findChild<QGroupBox*>("grpVolManager")->setEnabled(true);
        this->findChild<QGroupBox*>("grpSeg")->setEnabled(false);
    } else if (fWindowState == EWindowState::WindowStateDrawPath) {
        fVolumeViewerWidget->SetViewState(
            CVolumeViewerWithCurve::EViewState::ViewStateDraw);
        this->findChild<QGroupBox*>("grpVolManager")->setEnabled(false);
        this->findChild<QGroupBox*>("grpSeg")->setEnabled(false);
    } else if (fWindowState == EWindowState::WindowStateSegmentation) {
        fVolumeViewerWidget->SetViewState(
            CVolumeViewerWithCurve::EViewState::ViewStateEdit);
        this->findChild<QGroupBox*>("grpVolManager")->setEnabled(false);
        this->findChild<QGroupBox*>("grpSeg")->setEnabled(true);
    } else {
        // something else
    }

    fEdtStartIndex->setEnabled(false);

    fVolumeViewerWidget->UpdateView();

    update();
}

// Reset point cloud
void CWindow::ResetPointCloud(void)
{
    for (auto& seg : fSegStructMap) {
        auto& segStruct = seg.second;
        segStruct.ResetPointCloud();
    }
}

// Activate a specific segmentation by ID
void CWindow::ChangePathItem(std::string segID)
{
    statusBar->clearMessage();
    fSegmentationId = segID;

    // write new Segment to fSegStructMap
    fSegStructMap[fSegmentationId] = SegmentationStruct(fVpkg, segID, fPathOnSliceIndex);

    if (fSegStructMap[fSegmentationId].currentVolume != nullptr && fSegStructMap[fSegmentationId].fSegmentation->hasVolumeID()) {
        currentVolume = fSegStructMap[fSegmentationId].currentVolume;
    }

    // Only change slices if no other segmentations are being displayed
    bool setPathIndex = true;
    for (auto& seg : fSegStructMap) {
        auto& segStruct = seg.second;
        if (segStruct.display || segStruct.compute) {
            setPathIndex = false;
            break;
        }
    }
    if (setPathIndex && !fSegStructMap[fSegmentationId].fMasterCloud.empty()) {
        fPathOnSliceIndex = fSegStructMap[fSegmentationId].fPathOnSliceIndex;
    }

    OpenSlice();
    SetCurrentCurve(fPathOnSliceIndex);
    UpdateView();
}

// Deactivate a specific segmentation by ID. TODO: finish implementation?
void CWindow::RemovePathItem(std::string segID)
{
    statusBar->clearMessage();
}

// Split fMasterCloud into fUpperCloud and fLowerCloud
void CWindow::SplitCloud(void)
{
    for (auto& seg : fSegStructMap) {
        auto& segStruct = seg.second;
        segStruct.SetPathOnSliceIndex(fPathOnSliceIndex);
        segStruct.SplitCloud();
    }
}

// Do segmentation given the starting point cloud
void CWindow::DoSegmentation(void)
{
    statusBar->clearMessage();

    // Make sure our seg params structure has the current values
    if (not SetUpSegParams()) {
        QMessageBox::information(
            this, tr("Info"), tr("Invalid parameter for segmentation"));
        return;
    }

    // Setup LRPS
    auto segIdx = this->ui.cmbSegMethods->currentIndex();
    // Reminder to activate the segments for computation
    bool segmentedSomething = false;
    for (auto& seg : fSegStructMap) {
        auto& segStruct = seg.second;
        auto& segID = seg.first;

        // qDebug() << "Segment " << segID.c_str() << " display: " << segStruct.display << " compute: " << segStruct.compute;

        // if the segmentation is not being computed, skip it
        if (!segStruct.display || !segStruct.compute) {
            continue;
        }

        // if the segment is not on the starting index, skip it
        if (segStruct.fStartingPath.empty()) {
            // qDebug() << "Segment " << segID.c_str() << " has no starting path!";
            continue;
        }

        segmentedSomething = true;

        // If the segmentation starting curve was manually changed, we now need to merge it into the point cloud
        // that is going to get used for the segmentation since otherwise the manual changes would
        // be lost and the original curve would be used as starting point for the segmentation.
        seg.second.MergeChangedCurveIntoPointCloud(fEdtStartIndex->value());
        // Now we can forget all other changed curves
        seg.second.ForgetChangedCurves();


        Segmenter::Pointer segmenter;
        if (segIdx == 0) {
            auto lrps = vcs::LocalResliceSegmentation::New();
            lrps->setMaterialThickness(fVpkg->materialThickness());
            lrps->setTargetZIndex(fSegParams.targetIndex);
            lrps->setOptimizationIterations(fSegParams.fNumIters);
            lrps->setResliceSize(fSegParams.fWindowWidth);
            lrps->setAlpha(fSegParams.fAlpha);
            lrps->setK1(fSegParams.fK1);
            lrps->setK2(fSegParams.fK2);
            lrps->setBeta(fSegParams.fBeta);
            lrps->setDelta(fSegParams.fDelta);
            lrps->setDistanceWeightFactor(fSegParams.fPeakDistanceWeight);
            lrps->setConsiderPrevious(fSegParams.fIncludeMiddle);
            segmenter = lrps;
        }
        if (segIdx == 1) {
            auto ofsc = vcs::OpticalFlowSegmentationClass::New();
            ofsc->setMaterialThickness(fVpkg->materialThickness());
            ofsc->setTargetZIndex(fSegParams.targetIndex);
            ofsc->setOptimizationIterations(fSegParams.fNumIters);
            ofsc->setOutsideThreshold(fSegParams.outside_threshold);
            ofsc->setOFThreshold(fSegParams.optical_flow_pixel_threshold);
            ofsc->setOFDispThreshold(fSegParams.optical_flow_displacement_threshold);
            ofsc->setLineSmoothenByBrightness(fSegParams.smoothen_by_brightness);
            ofsc->setEdgeJumpDistance(fSegParams.edge_jump_distance);
            ofsc->setEdgeBounceDistance(fSegParams.edge_bounce_distance);
            ofsc->setEnableSmoothenOutlier(fSegParams.enable_smoothen_outlier);
            ofsc->setEnableEdge(fSegParams.enable_edge);
            ofsc->setPurgeCache(fSegParams.purge_cache);
            ofsc->setCacheSlices(fSegParams.cache_slices);
            ofsc->setOrderedPointSet(fSegStructMap[segID].fMasterCloud);
            ofsc->setBackwardsInterpolationWindow(fSegParams.backwards_smoothnes_interpolation_window);
            ofsc->setBackwardsLength(fSegParams.backwards_length);
            segmenter = ofsc;
        }
        // ADD OTHER SEGMENTER SETUP HERE. MATCH THE IDX TO THE IDX IN THE
        // DROPDOWN LIST

        // set common parameters
        segmenter->setChain(fSegStructMap[segID].fStartingPath);
        segmenter->setVolume(currentVolume);
        // Que Segmentation for execution
        queueSegmentation(segID, segmenter);
    }

    if (!segmentedSomething) {
        QMessageBox::warning(
            this, "Warning", "No Segments for computation found! Please activate segments for computation in the segment manager and make sure to be on a slice containing at least one curve.");
        segmentationQueue = std::queue<std::pair<std::string, Segmenter::Pointer>>();
    }

    executeNextSegmentation();
    // Start
}

void CWindow::queueSegmentation(std::string segmentationId, Segmenter::Pointer s)
{
    segmentationQueue.push(std::make_pair(segmentationId, s));
}

void CWindow::executeNextSegmentation()
{
    if (!segmentationQueue.empty()) {
        auto[segmentId, nextSegmenter]  = segmentationQueue.front();
        submittedSegmentationId = segmentId;
        segmentationQueue.pop();
        submitSegmentation(nextSegmenter);
        setWidgetsEnabled(false);
        worker_progress_.show();
        worker_progress_updater_.start();
    }
    else {
        setWidgetsEnabled(true);
        // set display to target layer
        fPathOnSliceIndex = fSegParams.targetIndex;
        CleanupSegmentation();
        SetUpCurves();
        UpdateView();
        playPing();
    }
}

void CWindow::audio_callback(void *user_data, Uint8 *raw_buffer, int bytes) {
        Sint16 *buffer = reinterpret_cast<Sint16*>(raw_buffer);
        int length = bytes / 2; // 2 bytes per sample for AUDIO_S16SYS
        int &sample_nr = *reinterpret_cast<int*>(user_data);

        for(int i = 0; i < length; i++, sample_nr++)
        {
            double time = static_cast<double>(sample_nr) / FREQUENCY;
            // This will give us a sine wave at 440 Hz
            buffer[i] = static_cast<Sint16>(AMPLITUDE * std::sin(2.0f * 3.14159f * 440.0f * time));
        }
    }

void CWindow::playPing() {
    SDL_AudioSpec desiredSpec;

    desiredSpec.freq = FREQUENCY;
    desiredSpec.format = AUDIO_S16SYS;
    desiredSpec.channels = 0;
    desiredSpec.samples = 2048;
    desiredSpec.callback = audio_callback;

    int sample_nr = 0;

    desiredSpec.userdata = &sample_nr;

    SDL_AudioSpec obtainedSpec;

    // you might want to look for errors here
    SDL_OpenAudio(&desiredSpec, &obtainedSpec);

    // start play audio
    SDL_PauseAudio(0);

    // play for 1000 milliseconds (1.0 second)
    SDL_Delay(1000);

    // Stop audio playback
    SDL_PauseAudio(1);

    SDL_CloseAudio();
}

void CWindow::onSegmentationFinished(Segmenter::PointSet ps)
{
    worker_progress_updater_.stop();
    worker_progress_.close();
    // 3) concatenate the two parts to form the complete point cloud
    // find starting location in fMasterCloud

    fSegStructMap[submittedSegmentationId].MergePointSetIntoPointCloud(ps);

    // qDebug() << "Segmentation finished: " << submittedSegmentationId.c_str();
    // for (int u = 0; u < fSegStructMap[submittedSegmentationId].fMasterCloud.height(); u++) {
    //     auto masterRowI = fSegStructMap[submittedSegmentationId].fMasterCloud.getRow(u);
    //     qDebug() << "Row " << u << " has " << masterRowI.size() << " points. With z: " << masterRowI[fSegStructMap[submittedSegmentationId].fUpperPart.width()-1][2];
    // }

    statusBar->showMessage(tr("Segmentation complete"));
    fVpkgChanged = true;

    // Execute the next segmentation
    executeNextSegmentation();
}

void CWindow::onSegmentationFailed(std::string s)
{
    vc::Logger()->error("Segmentation failed: {}", s);
    statusBar->showMessage(tr("Segmentation failed"));
    QMessageBox::critical(
        this, tr("VC"), QString::fromStdString("Segmentation failed:\n\n" + s));

    // Execute the next segmentation
    executeNextSegmentation();

    // setWidgetsEnabled(true);
    // worker_progress_updater_.stop();
    // worker_progress_.close();
    // CleanupSegmentation();
    // UpdateView();
}

void CWindow::onShowStatusMessage(QString text, int timeout)
{
    statusBar->showMessage(text, timeout);
}

void CWindow::CleanupSegmentation(void)
{
    for (auto& seg : fSegStructMap) {
        seg.second.ForgetChangedCurves();
    }

    fSegTool->setChecked(false);
    fWindowState = EWindowState::WindowStateIdle;
    SetUpCurves();
    OpenSlice();
    SetCurrentCurve(fPathOnSliceIndex);
}

// Set up the parameters for doing segmentation
bool CWindow::SetUpSegParams(void)
{
    bool aIsOk;

    double alpha = fEdtAlpha->text().toDouble(&aIsOk);
    if (aIsOk) {
        fSegParams.fAlpha = alpha;
    } else {
        return false;
    }

    double beta = fEdtBeta->text().toDouble(&aIsOk);
    if (aIsOk) {
        fSegParams.fBeta = beta;
    } else {
        return false;
    }

    double delta = fEdtDelta->text().toDouble(&aIsOk);
    if (aIsOk) {
        fSegParams.fDelta = delta;
    } else {
        return false;
    }

    double k1 = fEdtK1->text().toDouble(&aIsOk);
    if (aIsOk) {
        fSegParams.fK1 = k1;
    } else {
        return false;
    }

    double k2 = fEdtK2->text().toDouble(&aIsOk);
    if (aIsOk) {
        fSegParams.fK2 = k2;
    } else {
        return false;
    }

    int aNewVal = fEdtDistanceWeight->text().toInt(&aIsOk);
    if (aIsOk) {
        fSegParams.fPeakDistanceWeight = aNewVal;
    } else {
        return false;
    }

    fSegParams.fWindowWidth = fEdtWindowWidth->value();

    fSegParams.fIncludeMiddle = fOptIncludeMiddle->isChecked();

    // ending slice index
    aNewVal = fEdtEndIndex->text().toInt(&aIsOk);
    if (aIsOk &&
        aNewVal < currentVolume->numSlices()) {
        fSegParams.targetIndex = aNewVal;
    } else {
        return false;
    }

    return true;
}

// Get the curves for all the slices
void CWindow::SetUpCurves(void)
{
    // if (fVpkg == nullptr || fSegStructMap[fSegmentationId].fMasterCloud.empty()) {
    //     statusBar->showMessage(tr("Selected point cloud is empty"));
    //     vc::Logger()->warn("Segmentation point cloud is empty");
    //     return;
    // }
    for (auto& seg : fSegStructMap) {
        auto& segStruct = seg.second;
        segStruct.SetUpCurves();
    }
}

// Set the current curve
void CWindow::SetCurrentCurve(int nCurrentSliceIndex)
{
    // qDebug() << "SetCurrentCurve: " << nCurrentSliceIndex;
    for (auto& seg : fSegStructMap) {
        auto& segStruct = seg.second;
        segStruct.SetCurrentCurve(nCurrentSliceIndex);
    }
}

void CWindow::prefetchSlices(void) {
  while (true) {
    std::unique_lock<std::mutex> lk(cv_m);
    cv.wait(lk, [this]{return prefetchSliceIndex != -1;});

    if (stopPrefetching.load()) {
      break;
    }

    QSettings settings("VC.ini", QSettings::IniFormat);
    int prefetchWindow = settings.value("perf/preloaded_slices", 200).toInt() / 2;
    int currentSliceIndex = prefetchSliceIndex.load();
    int start = std::max(0, currentSliceIndex - prefetchWindow);
    int end = std::min(currentVolume->numSlices()-1, currentSliceIndex + prefetchWindow);

    int n = 5;  // Number Fetching Threads
    // fetching from index outwards
    for (int offset = 0; offset <= prefetchWindow; offset = offset + n) {
        std::vector<std::thread> threads;

        for (int i = 0; i <= n; i++) {
            // Fetch the slice data on the right side
            if (currentSliceIndex + offset + i <= end) {
                threads.emplace_back(&volcart::Volume::getSliceData, currentVolume, currentSliceIndex + offset + i);
            }
            // Fetch the slice data on the left side
            if (currentSliceIndex - offset - i >= start) {
                threads.emplace_back(&volcart::Volume::getSliceData, currentVolume, currentSliceIndex - offset - i);
            }
        }

        for (auto& t : threads) {
            t.join();
        }

        // Check if prefetching was stopped or slice index changed
        if (stopPrefetching.load() || prefetchSliceIndex.load() != currentSliceIndex) {
            break;
        }
    }

    prefetchSliceIndex = -1;
  }
}

// Function to start prefetching around a certain slice
void CWindow::startPrefetching(int index) {
  prefetchSliceIndex = index;
  cv.notify_one();
}

// Open slice
void CWindow::OpenSlice(void)
{
    cv::Mat aImgMat;
    if (fVpkg != nullptr) {
        // Stop prefetching
        prefetchSliceIndex = -1;
        cv.notify_one();

        aImgMat = currentVolume->getSliceDataCopy(fPathOnSliceIndex);
        aImgMat.convertTo(aImgMat, CV_8UC1, 1.0 / 256.0);
        //        cvtColor(aImgMat, aImgMat, cv::COLOR_GRAY2BGR);
    } else {
        aImgMat = cv::Mat::zeros(10, 10, CV_8UC1);
    }

    if (aImgMat.empty()) {
        auto h = currentVolume->sliceHeight();
        auto w = currentVolume->sliceWidth();
        aImgMat = cv::Mat::zeros(h, w, CV_8UC3);
        aImgMat = vc::color::RED;
        const std::string msg{"FILE MISSING"};
        auto params = CalculateOptimalTextParams(msg, w, h);
        auto originX = (w - params.size.width) / 2;
        auto originY = params.size.height + (h - params.size.height) / 2;
        cv::Point origin{originX, originY};
        cv::putText(
            aImgMat, msg, origin, params.font, params.scale, vc::color::WHITE,
            params.thickness, params.baseline);
    }

    auto aImgQImage = Mat2QImage(aImgMat);
    fVolumeViewerWidget->SetImage(aImgQImage);
    fVolumeViewerWidget->SetImageIndex(fPathOnSliceIndex);
}

// Initialize path list
void CWindow::InitPathList(void)
{
    fPathListWidget->clear();
    if (fVpkg != nullptr) {
        // show the existing paths
        for (auto& s : fVpkg->segmentationIDs()) {
            QTreeWidgetItem *item = new QTreeWidgetItem(fPathListWidget);
            item->setText(0, QString(s.c_str()));
            item->setCheckState(1, Qt::Unchecked);
            item->setCheckState(2, Qt::Unchecked);
        }

        // A bit hacky, but using QHeaderView::ResizeToContents did result in weird scrollbars
        fPathListWidget->resizeColumnToContents(0);
        fPathListWidget->resizeColumnToContents(1);
        fPathListWidget->resizeColumnToContents(2);
    }
}

// Update the Master cloud with the path we drew
void CWindow::SetPathPointCloud(void)
{
    // calculate the path and save that to a MasterCloud
    std::vector<cv::Vec2f> aSamplePts;
    fSplineCurve.GetSamplePoints(aSamplePts);

    // remove duplicates
    auto numPts = aSamplePts.size();
    auto unique = std::unique(aSamplePts.begin(), aSamplePts.end());
    aSamplePts.erase(unique, aSamplePts.end());
    auto uniquePts = aSamplePts.size();
    vc::Logger()->warn("Removed {} duplicate points", numPts - uniquePts);

    // setup a new master cloud
    fSegStructMap[fSegmentationId].fMasterCloud.setWidth(aSamplePts.size());
    std::vector<cv::Vec3d> points;
    for (const auto& pt : aSamplePts) {
        points.emplace_back(pt[0], pt[1], fPathOnSliceIndex);
    }
    fSegStructMap[fSegmentationId].fMasterCloud.pushRow(points);

    fSegStructMap[fSegmentationId].fMinSegIndex = static_cast<int>(floor(fSegStructMap[fSegmentationId].fMasterCloud[0][2]));
    fSegStructMap[fSegmentationId].fMaxSegIndex = fSegStructMap[fSegmentationId].fMinSegIndex;
}

// Open volume package
void CWindow::OpenVolume(const QString& path)
{
    QString aVpkgPath = path;
    QSettings settings("VC.ini", QSettings::IniFormat);

    if(aVpkgPath.isEmpty()) {
        aVpkgPath = QFileDialog::getExistingDirectory(
            this, tr("Open Directory"), settings.value("volpkg/default_path").toString(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks | QFileDialog::ReadOnly | QFileDialog::DontUseNativeDialog);
        // Dialog box cancelled
        if (aVpkgPath.length() == 0) {
            vc::Logger()->info("Open .volpkg canceled");
            return;
        }
    }

    // Checks the folder path for .volpkg extension
    auto const extension = aVpkgPath.toStdString().substr(
        aVpkgPath.toStdString().length() - 7, aVpkgPath.toStdString().length());
    if (extension != ".volpkg") {
        QMessageBox::warning(
            this, tr("ERROR"),
            "The selected file is not of the correct type: \".volpkg\"");
        vc::Logger()->error(
            "Selected file is not .volpkg: {}", aVpkgPath.toStdString());
        fVpkg = nullptr;  // Is need for User Experience, clears screen.
        return;
    }

    // Open volume package
    if (!InitializeVolumePkg(aVpkgPath.toStdString() + "/")) {
        return;
    }

    // Check version number
    if (fVpkg->version() != VOLPKG_SUPPORTED_VERSION) {
        const auto msg = "Volume package is version " +
                         std::to_string(fVpkg->version()) +
                         " but this program requires version " +
                         std::to_string(VOLPKG_SUPPORTED_VERSION) + ".";
        vc::Logger()->error(msg);
        QMessageBox::warning(this, tr("ERROR"), QString(msg.c_str()));
        fVpkg = nullptr;
        return;
    }

    fVpkgPath = aVpkgPath;
    fPathOnSliceIndex = 0;
    currentVolume = fVpkg->volume();
    // The cache should be at least as big as the number of preloaded slices, since otherwise,
    // many would immediately get purged again.
    // Note: This value might get overwritten by algorithm parameters.
    currentVolume->setCacheCapacity(settings.value("perf/preloaded_slices", 200).toInt());
    {
        const QSignalBlocker blocker{volSelect};
        volSelect->clear();
    }
    QStringList volIds;
    for (const auto& id : fVpkg->volumeIDs()) {
        volSelect->addItem(QString("%1 (%2)").arg(QString::fromStdString(id)).arg(QString::fromStdString(fVpkg->volume(id)->name())), QVariant(QString::fromStdString(id)));
    }

    UpdateRecentVolpkgList(aVpkgPath);
}

void CWindow::CloseVolume(void)
{
    fVpkg = nullptr;
    fSegmentationId = "";
    fSegmentation = nullptr;
    currentVolume = nullptr;
    fWindowState = EWindowState::WindowStateIdle;  // Set Window State to Idle
    fPenTool->setChecked(false);                   // Reset PenTool Button
    fSegTool->setChecked(false);                   // Reset Segmentation Button
    ResetPointCloud();
    OpenSlice();
    InitPathList();
    UpdateView();
}

// Handle open request
void CWindow::Open(void)
{
    Open(QString());
}

// Handle open request
void CWindow::Open(const QString& path)
{
    if (SaveDialog() == SaveResponse::Cancelled)
        return;

    CloseVolume();
    OpenVolume(path);
    OpenSlice();
    InitPathList();
    UpdateView();  // update the panel when volume package is loaded
}

void CWindow::OpenRecent()
{
    auto action = qobject_cast<QAction*>(sender());
    if(action)
        Open(action->data().toString());
}

// Close application
void CWindow::Close(void) { close(); }

// Pop up about dialog
void CWindow::Keybindings(void)
{
    // REVISIT - FILL ME HERE
    QMessageBox::information(
        this, tr("Keybindings for Volume Cartographer"),
        tr("Keyboard: \n"
        "------------------- \n"
        "Ctrl+O: Open Volume Package \n"
        "Ctrl+S: Save Volume Package \n"
        "A,D: Impact Range down/up \n"
        "[, ]: Alternative Impact Range down/up \n"
        "Q,E: Slice scan range down/up (mouse wheel scanning) \n"
        "Arrow Left/Right: Slice down/up by 1 \n"
        "1,2: Slice down/up by 1 \n"
        "3,4: Slice down/up by 2 \n"
        "5,6: Slice down/up by 5 \n"
        "7,8: Slice down/up by 10 \n"
        "9,0: Slice down/up by 100 \n"
        "Ctrl+G: Go to slice (opens dialog to insert slice index) \n"
        "T: Segmentation Tool \n"
        "P: Pen Tool \n"
        "Space: Toggle Curve Visibility \n"
        "C: Alternate Toggle Curve Visibility \n"
        "J: Highlight Next Curve that is selected for Computation \n"
        "K: Highlight Previous Curve that is selected for Computation \n"
        "F: Return to slice that the currently active tool was started on \n"
        "\n"
        "Mouse: \n"
        "------------------- \n"
        "Mouse Wheel: Scroll up/down \n"
        "Mouse Wheel + Alt: Scroll left/right \n"
        "Mouse Wheel + Ctrl: Zoom in/out \n"
        "Mouse Wheel + Shift: Next/previous slice \n"
        "Mouse Wheel + W Key Hold: Change impact range \n"
        "Mouse Wheel + R Key Hold: Follow Highlighted Curve \n"
        "Mouse Left Click: Add Points to Curve in Pen Tool. Snap Closest Point to Cursor in Segmentation Tool. \n"
        "Mouse Left Drag: Drag Point / Curve after Mouse Left Click \n"
        "Mouse Right Drag: Pan slice image\n"
        "Mouse Back/Forward Button: Follow Highlighted Curve \n"
        "Highlighting Segment ID: Shift/(Alt as well as Ctrl) Modifier to jump to Segment start/end."));
}

// Pop up about dialog
void CWindow::About(void)
{
    // REVISIT - FILL ME HERE
    QMessageBox::information(
        this, tr("About Volume Cartographer"),
        tr("Vis Center, University of Kentucky\n\n"
        "Fork: https://github.com/spacegaier/volume-cartographer"));
}

void CWindow::ShowSettings()
{
    auto pDlg = new SettingsDialog(this);
    pDlg->exec();
    delete pDlg;
}

void CWindow::PrintDebugInfo()
{
    // Add whatever should be printed via std::count via the action in the help menu.
    // Note: The menu entry is only visible with the matching INI entry.
}

// Save point cloud to path directory
void CWindow::SavePointCloud()
{
    int count = 0;
    int total = 0;
    for (auto& seg : fSegStructMap) {
        total++;
        auto& segStruct = seg.second;

        if (segStruct.fMasterCloud.empty() || segStruct.fSegmentationId.empty()) {
            qDebug() << "Empty cloud or segmentation ID to save for ID " << segStruct.fSegmentationId.c_str();
            continue;
        }
        // Try to save point cloud to volpkg
        try {
            segStruct.fSegmentation->setPointSet(segStruct.fMasterCloud);
            segStruct.fSegmentation->setVolumeID(currentVolume->id());
        } catch (std::exception& e) {
            QMessageBox::warning(
                this, "Error", "Failed to write cloud to volume package.");
            qDebug() << "Exception in save for ID " << segStruct.fSegmentationId.c_str();
            continue;
        }
        count++;
    }

    std::string saveMessage = "Saved " + std::to_string(count) + " Volume Package(s) of " + std::to_string(total) + ".";
    const char* saveMessageChar = saveMessage.c_str();
    statusBar->showMessage(tr(saveMessageChar), 5000);
    vc::Logger()->info(saveMessageChar);
    fVpkgChanged = false;
}

// Create new path
void CWindow::OnNewPathClicked(void)
{
    // Save if we need to
    if (SaveDialog() == SaveResponse::Cancelled) {
        return;
    }

    // Make a new segmentation in the volpkg
    volcart::DiskBasedObjectBaseClass::Identifier newSegmentationId;
    try {
        auto seg = fVpkg->newSegmentation();
        newSegmentationId = seg->id();
    } catch(std::exception) {
        // Could e.g. happen if the user clicks too quickly in succession on the "New" button as the timestamp
        // is the segment UUID, which would not be unique if there are two clicks within one second.
        QMessageBox::warning(this, tr("Error"), tr("An error occurred during segment creation. Please try again"));
        return;
    }

    // Add a new path to the tree widget
    QTreeWidgetItem *newItem = new QTreeWidgetItem(fPathListWidget);
    newItem->setText(0, QString(newSegmentationId.c_str()));
    newItem->setCheckState(1, Qt::Unchecked);
    newItem->setCheckState(2, Qt::Unchecked);

    // Activate the new item
    fPathListWidget->setCurrentItem(newItem);
    ChangePathItem(newSegmentationId); // Creating new curve
    newItem->setCheckState(1, Qt::Checked);
    newItem->setCheckState(2, Qt::Checked);
    fSegStructMap[newSegmentationId].display = true;
    fSegStructMap[newSegmentationId].compute = true;
    UpdateView();

    // A bit hacky, but using QHeaderView::ResizeToContents did result in weird scrollbars
    fPathListWidget->resizeColumnToContents(0);
    fPathListWidget->resizeColumnToContents(1);
    fPathListWidget->resizeColumnToContents(2);
}

// Remove existing path
void CWindow::OnRemovePathClicked(void)
{
    // If there is no current item, we cannot remove it
    if(!fPathListWidget->currentItem())
        return;

    auto id = fPathListWidget->currentItem()->text(0);

    if(!id.isEmpty()) {

        // Ask for user confirmation
        auto button = QMessageBox::critical(this, tr("Are you sure?"), tr("Warning: This will irrevocably delete the segment %1.\n\nThis action cannot be undone!\n\nContinue?").arg(id), QMessageBox::Yes | QMessageBox::No);

        if(button == QMessageBox::Yes) {

            try {
                if(fVpkg->removeSegmentation(id.toStdString())) {
                    fSegStructMap[id.toStdString()].ResetPointCloud();
                    delete fPathListWidget->currentItem();
                }
            } catch(std::exception) {
                QMessageBox::warning(this, tr("Error"), tr("An error occurred during segment removal."));
                return;
            }

            UpdateView();
        }
    }
}

void CWindow::UpdateSegmentCheckboxes(std::string aSegID) {
    if (aSegID.empty()) {
        // qDebug() << "UpdateSegmentCheckboxes: aSegID is empty";
        return;
    }
    if (fSegStructMap[aSegID].display || fSegStructMap[aSegID].compute) {
        // Disable all other new and empty Segmentations if new Segmentation created
        if (!fSegStructMap[aSegID].fSegmentationId.empty() && fSegStructMap[aSegID].fMasterCloud.empty()) {
            // qDebug() << "Disable all other new and empty Segmentations";
            for(auto& seg : fSegStructMap) {
                if (!seg.second.fSegmentationId.empty() && seg.first != aSegID && seg.second.fMasterCloud.empty()) {
                    seg.second.display = false;
                    seg.second.compute = false;
                    // qDebug() << "Compute " << seg.first.c_str() << " set compute false. while clicked on " << aSegID.c_str();
                    // uncheck the checkbox
                    QList<QTreeWidgetItem*> previousItems = fPathListWidget->findItems(
                        QString(seg.first.c_str()), Qt::MatchExactly, 0);
                    if (!previousItems.isEmpty())
                    {
                        previousItems[0]->setCheckState(1, Qt::Unchecked);
                        previousItems[0]->setCheckState(2, Qt::Unchecked);
                    }
                }
            }
        }

        // Disable all empty Segmentations if Segmentation with point cloud is enabled
        if (!fSegStructMap[aSegID].fSegmentationId.empty() && !fSegStructMap[aSegID].fMasterCloud.empty()) {
            // qDebug() << "Disable all pen Segmentations";
            for(auto& seg : fSegStructMap) {
                if (!seg.second.fSegmentationId.empty() && seg.first != aSegID && seg.second.fMasterCloud.empty()) {
                    // qDebug() << "Disable " << seg.first.c_str() << " id " << seg.second.fSegmentationId.c_str() << " with current id segment clicked: " << aSegID.c_str();
                    seg.second.display = false;
                    // qDebug() << "Compute " << seg.first.c_str() << " set compute false. while clicked on " << aSegID.c_str();
                    seg.second.compute = false;
                    // uncheck the checkbox
                    QList<QTreeWidgetItem*> previousItems = fPathListWidget->findItems(
                        QString(seg.first.c_str()), Qt::MatchExactly, 0);
                    if (!previousItems.isEmpty())
                    {
                        previousItems[0]->setCheckState(1, Qt::Unchecked);
                        previousItems[0]->setCheckState(2, Qt::Unchecked);
                    }
                }
            }
        }
    }

    // Delete completely disabled Segmentations from fSegStructMap
    auto it = fSegStructMap.begin();
    while (it != fSegStructMap.end()) {
        if (!it->second.display && !it->second.compute) {
            it = fSegStructMap.erase(it);
        } else {
            ++it;
        }
    }
}

void CWindow::toggleDisplayAll(bool checked)
{
    std::string lastID;
    // Iterate through all the items in the QTreeWidget and update their state.
    QTreeWidgetItemIterator it(fPathListWidget);
    while (*it) {
        QTreeWidgetItem* item = *it;
        std::string aSegID = item->text(0).toStdString();
        lastID = aSegID;
        if (checked) {
            // If the button/checkbox for "Display All" is checked, set all items to "Checked" state.
            if (item->checkState(1) != Qt::Checked) {
                // Only call ChangePathItem if the state is actually changing.
                ChangePathItem(aSegID);
            }
            item->setCheckState(1, Qt::Checked);
            fSegStructMap[aSegID].display = true;
        } else {
            fchkComputeAll->setChecked(false);
            // If the button/checkbox for "Display All" is unchecked, set all items to "Unchecked" state.
            item->setCheckState(1, Qt::Unchecked);
            item->setCheckState(2, Qt::Unchecked);
            std::string aSegID = item->text(0).toStdString();
            fSegStructMap[aSegID].display = false;
            fSegStructMap[aSegID].compute = false;
        }
        ++it;
    }
    UpdateSegmentCheckboxes(lastID);
    UpdateView(); // Assuming this function updates the display.
}

void CWindow::toggleComputeAll(bool checked)
{
    std::string lastID;
    // Iterate through all the items in the QTreeWidget and update their state.
    QTreeWidgetItemIterator it(fPathListWidget);
    while (*it) {
        QTreeWidgetItem* item = *it;
        std::string aSegID = item->text(0).toStdString();
        lastID = aSegID;
        if (checked) {
            if (item->checkState(1) != Qt::Checked) {
                // Only call ChangePathItem if the state is actually changing.
                ChangePathItem(aSegID);
            }
            fchkDisplayAll->setChecked(true);
            // If the button/checkbox for "Compute All" is checked, set all items to "Checked" state.
            item->setCheckState(1, Qt::Checked);
            item->setCheckState(2, Qt::Checked);
            fSegStructMap[aSegID].compute = true;
            // Also check "Display" because we can't compute without displaying.
            item->setCheckState(1, Qt::Checked);
            fSegStructMap[aSegID].display = true;
        } else {
            // If the button/checkbox for "Compute All" is unchecked, set all items to "Unchecked" state.
            item->setCheckState(2, Qt::Unchecked);
            std::string aSegID = item->text(0).toStdString();
            fSegStructMap[aSegID].compute = false;
        }
        ++it;
    }
    UpdateSegmentCheckboxes(lastID);
    UpdateView(); // Assuming this function updates the display.
}

// Handle path item click event
void CWindow::OnPathItemClicked(QTreeWidgetItem* item, int column)
{
    std::string aSegID = item->text(0).toStdString();
    // qDebug() << "Item clicked: " << item->text(0) << " Column: " << column;
    // If the first checkbox (in column 1) is clicked
    if (column == 0) // Highlight the curve
    {
        for(auto& seg : fSegStructMap) {
            seg.second.highlighted = false;
        }

        // Check if aSegID is in fSegStructMap
        if (fSegStructMap.find(aSegID) != fSegStructMap.end()) {
            fSegStructMap[aSegID].highlighted = true;
        }

        // Go to starting position if Shift is pressed
        if (qga::keyboardModifiers() == Qt::ShiftModifier) {
            fPathOnSliceIndex = fSegStructMap[aSegID].fMinSegIndex;
            OpenSlice();
            SetCurrentCurve(fPathOnSliceIndex);
        }
        // Go to ending position if Alt or Ctrl is pressed
        else if (qga::keyboardModifiers() == Qt::AltModifier || qga::keyboardModifiers() == Qt::ControlModifier) {
            fPathOnSliceIndex = fSegStructMap[aSegID].fMaxSegIndex;
            OpenSlice();
            SetCurrentCurve(fPathOnSliceIndex);
        }
    }
    else if (column == 1) // Display
    {
        if (item->checkState(column) == Qt::Checked)
        {
            if (SaveDialog() == SaveResponse::Cancelled)
            {
                // Update the list to show the previous selection
                QList<QTreeWidgetItem*> previousItems = fPathListWidget->findItems(
                    QString(fSegmentationId.c_str()), Qt::MatchExactly, 0);

                if (!previousItems.isEmpty())
                {
                    fPathListWidget->setCurrentItem(previousItems[0]);
                }

                // Uncheck the checkbox
                item->setCheckState(column, Qt::Unchecked);
            }
            // qDebug() << "Display " << aSegID.c_str();
            ChangePathItem(aSegID);
            // qDebug() << "Display " << aSegID.c_str() << " set display true.";
            fSegStructMap[aSegID].display = true;
        }
        else
        {
            // Also Uncheck the second checkbox (Compute). Never Compute without displaying the Curve.
            item->setCheckState(2, Qt::Unchecked);
            fSegStructMap[aSegID].display = false;
            // qDebug() << "Compute " << aSegID.c_str() << " set compute false.";
            fSegStructMap[aSegID].compute = false;
        }
    }
    // If the second checkbox (in column 2) is clicked
    else if (column == 2) // Compute
    {
        if (item->checkState(column) == Qt::Checked)
        {
            // Only compute if the first checkbox (Display) is checked, so check it too
            // Check the first checkbox
            if (item->checkState(1) != Qt::Checked)
            {
                item->setCheckState(1, Qt::Checked);
                ChangePathItem(aSegID);
            }
            fSegStructMap[aSegID].display = true;
            fSegStructMap[aSegID].compute = true;
            // qDebug() << "Compute " << aSegID.c_str() << " set compute true.";
        }
        else {
            // qDebug() << "Compute " << aSegID.c_str() << " set compute false.";
            fSegStructMap[aSegID].compute = false;
        }
    }

    // Check if any other Segmentation has highlighted set to true
    bool anyHighlighted = false;
    for(auto& seg : fSegStructMap) {
        if (seg.second.highlighted) {
            anyHighlighted = true;
            break;
        }
    }

    // If no Segmentation has highlighted set to true, and current segment was checked, set highlight to true
    if (!anyHighlighted && item->checkState(1) == Qt::Checked) {
        fSegStructMap[aSegID].highlighted = true;
        // Set column 0 to selected (highlighted, since it is not a checkmark)
        item->setSelected(true);
    }

    UpdateSegmentCheckboxes(aSegID);

    UpdateView();
}

void CWindow::OnPathCustomContextMenu(const QPoint& point)
{
    QModelIndex index = fPathListWidget->indexAt(point);
    if (index.isValid()) {
        QAction* actVcRender = new QAction(tr("Run vc_render"), this);
        QString segID = fPathListWidget->itemFromIndex(index)->text(0);
        connect(actVcRender, &QAction::triggered, this, [segID, this](){ OnRunVcRender(segID); });

        QMenu menu(this);
        menu.addAction(actVcRender);

        menu.exec(fPathListWidget->viewport()->mapToGlobal(point));
    }
}

void CWindow::OnRunVcRender(QString segmentID)
{
    QString program = "./vc_render";
    QStringList arguments;
    arguments << "-v" << fVpkgPath << "-s" << segmentID << "-o" << QString("test_%1.obj").arg(segmentID) << "--uv-plot" << QString("test_uv_%1.tif").arg(segmentID) << "--mesh-resample-smoothing" << "3";

    //vc_render -v my-project.volpkg -s 20230503225234 -o test_20230503225234.obj

    std::cout << "Starting vc_render for segment " << segmentID.toStdString() << std::endl;

    //std::cout << QDir::tempPath().toStdString() << std::endl;

    std::cout << "Used arguments: ";
    for(auto arg : arguments) {
        std::cout << arg.toStdString() << " ";
    }
    std::cout << std::endl;

    QProcess *myProcess = new QProcess(this);
    connect(myProcess, &QProcess::finished, this, [this, segmentID](){ std::cout << "Finished: " << segmentID.toStdString() << std::endl; });
    myProcess->start(program, arguments);
}

// Logic to switch the selected Id
void CWindow::PreviousSelectedId() {
    // seg that is currently highlighted
    std::string currentId;
    for(auto& seg : fSegStructMap) {
        if (seg.second.highlighted) {
            currentId = seg.first;
        }
        seg.second.highlighted = false;
    }
    // Find the previous seg that is active (compute or display)
    std::string previousId;
    for(auto& seg : fSegStructMap) {
        if (seg.first == currentId) {
            break;
        }
        if (seg.second.compute) {
            previousId = seg.first;
        }
    }
    // If no previous seg found, start from the end
    if (previousId.empty()) {
        for(auto& seg : fSegStructMap) {
            if (seg.second.compute) {
                previousId = seg.first;
            }
        }
    }
    // If still no previous seg found, return
    if (previousId.empty()) {
        return;
    }

    // Set the previous seg to highlighted
    fSegStructMap[previousId].highlighted = true;

    UpdateView();
}

// Logic to switch the selected Id
void CWindow::NextSelectedId() {
    // seg that is currently highlighted
    std::string currentId;
    for(auto& seg : fSegStructMap) {
        if (seg.second.highlighted) {
            currentId = seg.first;
        }
        seg.second.highlighted = false;
    }
    // Find the next seg that is active (compute or display)
    std::string nextId;
    bool found = false;
    for(auto& seg : fSegStructMap) {
        if (found && seg.second.compute) {
            nextId = seg.first;
            break;
        }
        if (seg.first == currentId) {
            found = true;
        }
    }
    // If no next seg found, start from the beginning
    if (nextId.empty()) {
        for(auto& seg : fSegStructMap) {
            if (seg.second.compute) {
                nextId = seg.first;
                break;
            }
        }
    }
    // If still no next seg found, return
    if (nextId.empty()) {
        return;
    }

    // Set the next seg to highlighted
    fSegStructMap[nextId].highlighted = true;

    UpdateView();
}

// Show go to slice dialog and execute the jump
void CWindow::ShowGoToSliceDlg() {
    if (currentVolume == nullptr || !fVolumeViewerWidget->fNextBtn->isEnabled()) {
        return;
    }

    bool status;
    const int sliceIndex = QInputDialog::getInt(this, tr("Go to slice"), tr("Slice Index"), 0, 0, currentVolume->numSlices(), 1, &status);

    if(status) {
        OnLoadAnySlice(sliceIndex);
    }
}

void CWindow::ScanRangeUp() {
    if(currentScanRangeIndex < std::size(scanRanges) - 1) {
        currentScanRangeIndex++;
    }

    // Always inform the UI/user, even if the value stayed the same
    fVolumeViewerWidget->SetScanRange(scanRanges[currentScanRangeIndex]);
}

void CWindow::ScanRangeDown() {
    if(currentScanRangeIndex > 0) {
        currentScanRangeIndex--;
        fVolumeViewerWidget->SetScanRange(scanRanges[currentScanRangeIndex]);
    }

    // Always inform the UI/user, even if the value stayed the same
    fVolumeViewerWidget->SetScanRange(scanRanges[currentScanRangeIndex]);
}

void CWindow::ReturnToEditSlice() {
    if(fSegTool->isChecked()) {
        fVolumeViewerWidget->ReturnToSliceIndexToolStart();
    }
}

// Logic to activate pen tool
void CWindow::ActivatePenTool() {
    // Pen tool available
    if (fPenTool->isEnabled()) {
        fPenTool->setChecked(!fPenTool->isChecked());
        TogglePenTool();
    }
}

// Logic to activate/deactivate segmentation tool
void CWindow::ActivateSegmentationTool() {
    // Segmentation tool available
    if (fSegTool->isEnabled()) {
        fSegTool->setChecked(!fSegTool->isChecked());
        ToggleSegmentationTool();
    }
}

// Toggle the status of the pen tool
void CWindow::TogglePenTool(void)
{
    if (fPenTool->isChecked()) {
        fWindowState = EWindowState::WindowStateDrawPath;
        fSliceIndexToolStart = fPathOnSliceIndex;
        fVolumeViewerWidget->SetSliceIndexToolStart(fSliceIndexToolStart);

        // turn off segmentation tool
        fSegTool->setChecked(false);
        fVolumeViewerWidget->setFocus();
    } else {
        fWindowState = EWindowState::WindowStateIdle;

        if (fSplineCurve.GetNumOfControlPoints() > 1) {
            SetPathPointCloud();  // finished drawing, set up path
            SavePointCloud();
            SetUpCurves();
            OpenSlice();
            SetCurrentCurve(fPathOnSliceIndex);
        }
        fSplineCurve.Clear();
        fVolumeViewerWidget->ResetSplineCurve();
        fSliceIndexToolStart = -1;
        fVolumeViewerWidget->SetSliceIndexToolStart(fSliceIndexToolStart);
    }

    UpdateView();
}

// Toggle the status of the segmentation tool
void CWindow::ToggleSegmentationTool(void)
{
    if (fSegTool->isChecked()) {
        // If the prefetching worker is not yet running, start it
        if (!prefetchWorker.joinable()) {
            prefetchWorker = std::thread(&CWindow::prefetchSlices, this);
        }
        // Start prefetching around the current slice
        startPrefetching(fPathOnSliceIndex);
        fSliceIndexToolStart = fPathOnSliceIndex;
        fVolumeViewerWidget->SetSliceIndexToolStart(fSliceIndexToolStart);

        fWindowState = EWindowState::WindowStateSegmentation;
        SplitCloud();

        // turn off pen tool
        fPenTool->setChecked(false);
        fVolumeViewerWidget->setFocus();
    } else {
        // Warn user that curve changes will get lost
        bool changesFound = false;
        for (auto& seg : fSegStructMap) {
            if(seg.second.HasChangedCurves()) {
                changesFound = true;
                break;
            }
        }

        if(changesFound) {
            const auto response = QMessageBox::question(this, "Changed Curves",
                tr("You have made changes to curves that will get lost if you exit without starting a segmentation run.\n\nDiscard the changes?"),
                QMessageBox::Discard | QMessageBox::Cancel);

            if(response == QMessageBox::Cancel) {
                fSegTool->setChecked(true);
                return;
            }
        }

        CleanupSegmentation();
        fSliceIndexToolStart = -1;
        fVolumeViewerWidget->SetSliceIndexToolStart(fSliceIndexToolStart);
    }
    UpdateView();
}

void CWindow::OnChangeSegAlgo(int index)
{
    this->ui.segParamsStack->setCurrentIndex(index);
}

// Handle gravity value change
void CWindow::OnEdtAlphaValChange()
{
    bool aIsOk;
    double aNewVal = fEdtAlpha->text().toDouble(&aIsOk);
    if (aIsOk) {
        if (aNewVal <= 0.0) {
            aNewVal = 0.0;
            fEdtAlpha->setText(QString::number(aNewVal));
        } else if (aNewVal > 1.0) {
            aNewVal = 1.0;
            fEdtAlpha->setText(QString::number(aNewVal));
        }
        fSegParams.fAlpha = aNewVal;
    }
}

void CWindow::OnEdtBetaValChange()
{
    bool aIsOk;
    double aNewVal = fEdtBeta->text().toDouble(&aIsOk);
    if (aIsOk) {
        if (aNewVal <= 0.0) {
            aNewVal = 0.0;
            fEdtBeta->setText(QString::number(aNewVal));
        } else if (aNewVal > 1.0) {
            aNewVal = 1.0;
            fEdtBeta->setText(QString::number(aNewVal));
        }
        fSegParams.fBeta = aNewVal;
    }
}

void CWindow::OnEdtDeltaValChange()
{
    bool aIsOk;
    double aNewVal = fEdtDelta->text().toDouble(&aIsOk);
    if (aIsOk) {
        if (aNewVal <= 0.0) {
            aNewVal = 0.0;
            fEdtDelta->setText(QString::number(aNewVal));
        } else if (aNewVal > 1.0) {
            aNewVal = 1.0;
            fEdtDelta->setText(QString::number(aNewVal));
        }
        fSegParams.fDelta = aNewVal;
    }
}

void CWindow::OnEdtK1ValChange()
{
    bool aIsOk;
    double aNewVal = fEdtK1->text().toDouble(&aIsOk);
    if (aIsOk) {
        if (aNewVal <= 0.0) {
            aNewVal = 0.0;
            fEdtK1->setText(QString::number(aNewVal));
        } else if (aNewVal > 1.0) {
            aNewVal = 1.0;
            fEdtK1->setText(QString::number(aNewVal));
        }
        fSegParams.fK1 = aNewVal;
    }
}

void CWindow::OnEdtK2ValChange()
{
    bool aIsOk;
    double aNewVal = fEdtK2->text().toDouble(&aIsOk);
    if (aIsOk) {
        if (aNewVal <= 0.0) {
            aNewVal = 0.0;
            fEdtK2->setText(QString::number(aNewVal));
        } else if (aNewVal > 1.0) {
            aNewVal = 1.0;
            fEdtK2->setText(QString::number(aNewVal));
        }
        fSegParams.fK2 = aNewVal;
    }
}

void CWindow::OnEdtDistanceWeightChange()
{
    bool aIsOk;
    int aNewVal = fEdtDistanceWeight->text().toInt(&aIsOk);
    if (aIsOk) {
        if (aNewVal > 100) {
            aNewVal = 100;
        } else if (aNewVal < 0) {
            aNewVal = 0;
        }
        fEdtDistanceWeight->setText(QString::number(aNewVal));
        fSegParams.fPeakDistanceWeight = aNewVal;
    }
}

void CWindow::OnEdtWindowWidthChange(int newVal)
{
    fSegParams.fWindowWidth = newVal;
}

void CWindow::OnOptIncludeMiddleClicked(bool clicked)
{
    fOptIncludeMiddle->setChecked(clicked);
    fSegParams.fIncludeMiddle = clicked;
}

/*
// Handle sample distance value change
void CWindow::OnEdtSampleDistValChange( QString nText )
{
    // REVISIT - the widget should be disabled and the change ignored for now
    bool aIsOk;
    int aNewVal = nText.toInt( &aIsOk );
    if ( aIsOk ) {
        fSegParams.fThreshold = aNewVal;
    }
}
*/

// Handle starting slice value change
void CWindow::OnEdtStartingSliceValChange(int index)
{
    // REVISIT - FILL ME HERE
    // REVISIT - should be equivalent to "set current slice", the same as
    // navigation through slices
}

// Handle ending slice value change
void CWindow::OnEdtEndingSliceValChange()
{
    // ending slice index
    int aNewVal = fEdtEndIndex->value();
    if (aNewVal < currentVolume->numSlices()) {
        fEndTargetOffset = aNewVal - fPathOnSliceIndex;
    } else {
        statusBar->showMessage(
            tr("ERROR: Selected slice is out of range of the volume!"), 10000);
        fEdtEndIndex->setValue(fPathOnSliceIndex + fEndTargetOffset);
    }
}

// Handle start segmentation
void CWindow::OnBtnStartSegClicked(void) { DoSegmentation(); }

// Handle changes to impact range
void CWindow::OnEdtImpactRange(int nImpactRangeIndex)
{
    // Translate value from slider (treated as index into steps) to actual impact range value
    auto impactRange = impactRangeSteps.at(nImpactRangeIndex);
    fVolumeViewerWidget->SetImpactRange(impactRange);
    fLabImpactRange->setText(QString::number(impactRange));
}

// Handle request to step impact range up
void CWindow::onImpactRangeUp(void)
{
    // Trigger an uptick in the slider
    if (ui.sldImpactRange->isEnabled()) {
        ui.sldImpactRange->triggerAction(QSlider::SliderAction::SliderSingleStepAdd);
    }
}

// Handle request to step impact range down
void CWindow::onImpactRangeDown(void)
{
    // Trigger an uptick in the slider
    if (ui.sldImpactRange->isEnabled()) {
        ui.sldImpactRange->triggerAction(QSlider::SliderAction::SliderSingleStepSub);
    }
}

// Handle loading any slice
void CWindow::OnLoadAnySlice(int slice)
{
    if (slice >= 0 && slice < currentVolume->numSlices()) {
        fPathOnSliceIndex = slice;
        OpenSlice();
        SetCurrentCurve(fPathOnSliceIndex);
        UpdateView();
    } else
        statusBar->showMessage(
            tr("ERROR: Selected slice is out of range of the volume!"), 10000);
}

void CWindow::OnLoadNextSliceShift(int shift)
{
    if (fPathOnSliceIndex + shift >= currentVolume->numSlices()) {
        shift = currentVolume->numSlices() - fPathOnSliceIndex - 1;
    }

    if (!fVolumeViewerWidget->fNextBtn->isEnabled()) {
        statusBar->showMessage(
            tr("Changing Slices is deactivated in the Pen Tool!"), 10000);
    } else if (shift != 0) {
        fPathOnSliceIndex += shift;
        OpenSlice();
        SetCurrentCurve(fPathOnSliceIndex);
        UpdateView();
    } else {
        statusBar->showMessage(tr("Already at the end of the volume!"), 10000);
    }
}

void CWindow::OnLoadPrevSliceShift(int shift)
{
    if (fPathOnSliceIndex - shift < 0) {
        shift = fPathOnSliceIndex;
    }

    if (!fVolumeViewerWidget->fPrevBtn->isEnabled()) {
        statusBar->showMessage(
            tr("Changing Slices is deactivated in the Pen Tool!"), 10000);
    } else if (shift != 0) {
        fPathOnSliceIndex -= shift;
        OpenSlice();
        SetCurrentCurve(fPathOnSliceIndex);
        UpdateView();
    } else {
        statusBar->showMessage(
            tr("Already at the beginning of the volume!"), 10000);
    }
}

// Handle path change event
void CWindow::OnPathChanged(void)
{
    if (fWindowState == EWindowState::WindowStateSegmentation) {
        for (auto& seg : fSegStructMap) {
            // update current segStruct
            seg.second.OnPathChanged();
        }
    }
}

bool CWindow::can_change_volume_()
{
    // return fVpkg != nullptr && fVpkg->numberOfVolumes() > 1 &&
    //        (fSegStructMap[fSegmentationId].fSegmentation == nullptr || !fSegStructMap[fSegmentationId].fSegmentation->hasPointSet() ||
    //         !fSegStructMap[fSegmentationId].fSegmentation->hasVolumeID());

    bool canChange = fVpkg != nullptr && fVpkg->numberOfVolumes() > 1;
    for (auto& seg : fSegStructMap) {
            auto& segStruct = seg.second;
            canChange = canChange && (segStruct.fSegmentation == nullptr || !segStruct.fSegmentation->hasPointSet() || !segStruct.fSegmentation->hasVolumeID());
    }
    return canChange;
}
