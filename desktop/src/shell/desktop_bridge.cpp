#include "desktop_bridge.hpp"

#include "desktop_actions.hpp"

#include <QDebug>

#include <nlohmann/json.hpp>

#include <exception>
#include <utility>

std::unique_ptr<DesktopBridge> DesktopBridge::create(QObject* parent)
{
    auto session = toi::model::DesktopSession::create();
    if (!session) {
        qFatal("Failed to create desktop session: %s", session.error().message.c_str());
    }
    return std::unique_ptr<DesktopBridge>(new DesktopBridge(std::move(*session), parent));
}

QString DesktopBridge::bootstrap()
{
    return QString::fromStdString(toi::desktop::dispatch_action(session_, {
        {"id", 0},
        {"method", "app.get_state"},
        {"params", nlohmann::json::object()},
    }).dump());
}

QString DesktopBridge::dispatch(const QString& request)
{
    try {
        const auto parsed = nlohmann::json::parse(request.toStdString());
        const std::string method = parsed.value("method", "");
        auto response = toi::desktop::dispatch_action(session_, parsed);
        if (response.value("ok", false) && toi::desktop::action_changes_preview(method)) {
            emit previewInvalidated();
        }
        return QString::fromStdString(response.dump());
    } catch (const std::exception& error) {
        return QString::fromStdString(nlohmann::json{
            {"id", nullptr},
            {"ok", false},
            {"error", error.what()},
        }.dump());
    }
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
