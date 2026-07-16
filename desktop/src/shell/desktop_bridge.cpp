#include "desktop_bridge.hpp"

#include "desktop_actions.hpp"

#include <QDebug>

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
}
