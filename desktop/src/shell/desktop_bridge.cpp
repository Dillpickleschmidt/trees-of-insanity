#include "desktop_bridge.hpp"

#include "desktop_actions.hpp"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <utility>

std::unique_ptr<DesktopBridge> DesktopBridge::create(QObject* parent)
{
    auto session = toi::model::DesktopSession::create();
    if (!session) {
        qFatal("Failed to create desktop session: %s", session.error().message.c_str());
    }
    return std::unique_ptr<DesktopBridge>(new DesktopBridge(std::move(*session), parent));
}

QString DesktopBridge::dispatch(const QString& request)
{
    auto result = toi::desktop::dispatch_action(session_, request.toStdString());
    if (result.preview_changed) {
        emit previewInvalidated();
    }
    switch (result.plant_run_control) {
    case toi::desktop::PlantRunControl::None:
        break;
    case toi::desktop::PlantRunControl::Start:
        startPlantRun();
        break;
    case toi::desktop::PlantRunControl::Stop:
        stopPlantRun();
        break;
    }
    if (result.viewport_frames_per_second) {
        viewport_frames_per_second_ = *result.viewport_frames_per_second;
        viewport_frame_interval_ms_ =
            std::max(1, static_cast<int>(std::ceil(1000.0 / viewport_frames_per_second_)));
        emit viewportFramesPerSecondChanged(viewport_frames_per_second_);
    }
    return QString::fromStdString(result.response);
}

void DesktopBridge::uiEvent(const QString& type, const QString& data)
{
    qInfo().noquote() << "ui:" + type << data;
}

void DesktopBridge::setViewportRect(double x, double y, double width, double height, double devicePixelRatio)
{
    emit viewportRectChanged(x, y, width, height, devicePixelRatio);
}

void DesktopBridge::cameraInput(const QString& kind, double dx, double dy, int viewportHeight)
{
    emit viewportCameraInput(kind, dx, dy, viewportHeight);
}

toi::model::DesktopSession& DesktopBridge::session()
{
    return session_;
}

double DesktopBridge::viewportFramesPerSecond() const
{
    return viewport_frames_per_second_;
}

void DesktopBridge::publishViewportStatus(const QString& status)
{
    emit viewportStatusChanged(status);
}

void DesktopBridge::publishPlantDiagnosticLabels(const QString& labels)
{
    emit plantDiagnosticLabelsChanged(labels);
}

DesktopBridge::DesktopBridge(toi::model::DesktopSession session, QObject* parent)
    : QObject(parent), session_(std::move(session))
{
    setObjectName(QStringLiteral("desktopBridge"));
    plant_run_timer_.setInterval(0);
    connect(&plant_run_timer_, &QTimer::timeout, this, &DesktopBridge::advancePlantRun);
}

void DesktopBridge::startPlantRun()
{
    if (plant_run_active_) {
        return;
    }
    plant_run_active_ = true;
    plant_preview_clock_.start();
    publishPlantRunProgress(true);
    plant_run_timer_.start();
}

void DesktopBridge::stopPlantRun(QString error)
{
    if (!plant_run_active_) {
        return;
    }
    plant_run_timer_.stop();
    plant_run_active_ = false;
    auto finished = session_.finish_plant_run();
    if (!finished && error.isEmpty()) {
        error = QString::fromStdString(finished.error().message);
    }
    emit previewInvalidated();
    publishPlantRunProgress(false, error);
}

void DesktopBridge::advancePlantRun()
{
    auto advanced = session_.advance_plant();
    if (!advanced) {
        stopPlantRun(QString::fromStdString(advanced.error().message));
        return;
    }
    if (advanced->reached_target) {
        stopPlantRun();
        return;
    }
    if (plant_preview_clock_.elapsed() >= viewport_frame_interval_ms_) {
        plant_preview_clock_.restart();
        emit previewInvalidated();
        publishPlantRunProgress(true);
    }
}

void DesktopBridge::publishPlantRunProgress(bool running, const QString& error)
{
    const auto state = toi::desktop::dispatch_action(session_, R"({"method":"plant.get_state"})");
    emit plantRunProgressChanged(QString::fromStdString(state.response), running, error);
}
