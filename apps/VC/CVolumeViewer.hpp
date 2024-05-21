// CVolumeViewer.h
// Chao Du 2015 April
#pragma once

#include <QtWidgets>
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QDebug>

#include "COverlayHandler.hpp"

namespace ChaoVis
{

class CVolumeViewerView : public QGraphicsView
{
    Q_OBJECT

    public:
        CVolumeViewerView(QWidget* parent = nullptr);

        void setup();

        void keyPressEvent(QKeyEvent* event) override;
        void keyReleaseEvent(QKeyEvent* event) override;

        bool isRangeKeyPressed() { return rangeKeyPressed; }
        bool isCurvePanKeyPressed() { return curvePanKeyPressed; }
        bool isRotateKyPressed() { return rotateKeyPressed; }

        void showTextAboveCursor(const QString& value, const QString& label, const QColor& color);
        void hideTextAboveCursor();

        void showCurrentImpactRange(int range);
        void showCurrentScanRange(int range);
        void showCurrentSliceIndex(int slice, bool highlight);

        void updateCurrentRotation(int delta) { currentRotation += delta; }
        auto getCurrentRotation() -> int { return currentRotation; }

    protected:
        bool rangeKeyPressed{false};
        bool curvePanKeyPressed{false};
        bool rotateKeyPressed{false};

        QGraphicsTextItem* textAboveCursor;
        QGraphicsRectItem* backgroundBehindText;
        QTimer* timerTextAboveCursor;

        // Required to be able to reset the rotation without also resetting the scaling
        int currentRotation{0};
};

class CVolumeViewer : public QWidget
{
    Q_OBJECT

public:
    enum EViewState {
        ViewStateEdit,  // edit mode
        ViewStateDraw,  // draw mode
        ViewStateIdle   // idle mode
    };

    QPushButton* fNextBtn;
    QPushButton* fPrevBtn;
    CVolumeViewer(QWidget* parent = 0);
    ~CVolumeViewer(void);
    virtual void setButtonsEnabled(bool state);

    void SetViewState(EViewState nViewState) { fViewState = nViewState; }
    EViewState GetViewState(void) { return fViewState; }
    auto GetView() -> QGraphicsView* { return fGraphicsView; }

    virtual void SetImage(const QImage& nSrc);
    void SetImageIndex(int nImageIndex)
    {
        fImageIndex = nImageIndex;
        fImageIndexEdit->setValue(nImageIndex);
        UpdateButtons();
    }
    auto GetImageIndex() const -> int { return fImageIndex; }
    void setNumSlices(int num);
    void ResetRotation();
    void SetOverlaySettings(COverlayHandler::OverlaySettings settings);

    void ScheduleOverlayUpdate();
    void UpdateOverlay();
    virtual void UpdateView() {};

protected:
    bool eventFilter(QObject* watched, QEvent* event);

public slots:
    void OnZoomInClicked(void);
    void OnZoomOutClicked(void);
    void OnResetClicked(void);
    void OnNextClicked(void);
    void OnPrevClicked(void);
    void OnImageIndexEditTextChanged(void);

signals:
    void SendSignalOnNextSliceShift(int shift);
    void SendSignalOnPrevSliceShift(int shift);
    void SendSignalOnLoadAnyImage(int nImageIndex);
    void SendSignalStatusMessageAvailable(QString text, int timeout);
    void SendSignalImpactRangeUp(void);
    void SendSignalImpactRangeDown(void);

protected:
    void ScaleImage(double nFactor);
    void CenterOn(const QPointF& point);
    virtual void UpdateButtons(void);

protected:
    // widget components
    CVolumeViewerView* fGraphicsView;
    QGraphicsScene* fScene;
    COverlayHandler* fOverlayHandler;

    QLabel* fCanvas;
    QScrollArea* fScrollArea;
    QPushButton* fZoomInBtn;
    QPushButton* fZoomOutBtn;
    QPushButton* fResetBtn;
    QSpinBox* fImageIndexEdit;
    QHBoxLayout* fButtonsLayout;

    // data
    EViewState fViewState;
    QImage* fImgQImage;
    double fScaleFactor;
    int fImageIndex;
    int sliceIndexToolStart{-1};
    int fScanRange;  // how many slices a mouse wheel step will jump

    // user settings
    bool fCenterOnZoomEnabled;
    int fScrollSpeed{-1};

    QGraphicsPixmapItem* fBaseImageItem;
    QList<COverlayGraphicsItem*> overlayItems;
    QTimer* timerOverlayUpdate;
};  // class CVolumeViewer

}  // namespace ChaoVis
