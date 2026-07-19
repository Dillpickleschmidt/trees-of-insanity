#pragma once

#include "toi/model/desktop_session.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>

class DesktopBridge final : public QObject {
    Q_OBJECT

public:
    static std::unique_ptr<DesktopBridge> create(QObject* parent = nullptr);

    Q_INVOKABLE QString dispatch(const QString& action);
    Q_INVOKABLE void uiEvent(const QString& type, const QString& data);
    Q_INVOKABLE void setViewportRect(double x, double y, double width, double height, double devicePixelRatio);
    Q_INVOKABLE void cameraInput(const QString& kind, double dx, double dy, int viewportHeight);

    toi::model::DesktopSession& session();
    [[nodiscard]] double viewportFramesPerSecond() const;
    void publishViewportStatus(const QString& status);
    void publishPlantDiagnosticLabels(const QString& labels);

signals:
    void viewportStatusChanged(const QString& status);
    void plantDiagnosticLabelsChanged(const QString& labels);
    void plantRunProgressChanged(const QString& state, bool running, const QString& error);
    void viewportRectChanged(double x, double y, double width, double height, double devicePixelRatio);
    void viewportCameraInput(QString kind, double dx, double dy, int viewportHeight);
    void viewportFramesPerSecondChanged(double framesPerSecond);
    void previewInvalidated();

private:
    explicit DesktopBridge(toi::model::DesktopSession session, QObject* parent);

    void startPlantRun();
    void stopPlantRun(QString error = {});
    void advancePlantRun();
    void publishPlantRunProgress(bool running, const QString& error = {});

    toi::model::DesktopSession session_;
    QTimer plant_run_timer_;
    QElapsedTimer plant_preview_clock_;
    int viewport_frame_interval_ms_ = 34;
    double viewport_frames_per_second_ = 30.0;
    bool plant_run_active_ = false;
};
