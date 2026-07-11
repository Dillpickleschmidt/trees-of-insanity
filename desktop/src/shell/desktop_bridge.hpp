#pragma once

#include "toi/model/desktop_session.hpp"

#include <QObject>
#include <QString>

#include <memory>

class DesktopBridge final : public QObject {
    Q_OBJECT

public:
    static std::unique_ptr<DesktopBridge> create(QObject* parent = nullptr);

    Q_INVOKABLE QString bootstrap();
    Q_INVOKABLE QString dispatch(const QString& action);
    Q_INVOKABLE void uiEvent(const QString& type, const QString& data);
    Q_INVOKABLE void setViewportRect(double x, double y, double width, double height, double devicePixelRatio);
    Q_INVOKABLE void cameraInput(const QString& kind, double dx, double dy, int viewportHeight);

    toi::model::DesktopSession& session();
    void publishViewportStatus(const QString& status);

signals:
    void viewportStatusChanged(const QString& status);
    void viewportRectChanged(double x, double y, double width, double height, double devicePixelRatio);
    void viewportCameraInput(QString kind, double dx, double dy, int viewportHeight);
    void previewInvalidated();

private:
    explicit DesktopBridge(toi::model::DesktopSession session, QObject* parent);

    toi::model::DesktopSession session_;
};
