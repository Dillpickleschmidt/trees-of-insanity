#include "desktop_bridge.hpp"
#include "vulkan_device.hpp"
#include "viewport_texture_item.hpp"
#if defined(TOI_ENABLE_OVRTX)
#include "toi/render/render_projection.hpp"
#include "toi/viewport/preview_renderer.hpp"
#endif

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <qqml.h>
#include <QQuickGraphicsConfiguration>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QVulkanInstance>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include <algorithm>
#include <cmath>
#include <memory>

#if defined(TOI_ENABLE_OVRTX)
namespace {

toi::model::Result<toi::render::GrowthPreviewStageProjection>
make_preview_projection(const toi::model::DesktopSession& session, int width = 1280, int height = 720)
{
    auto snapshot = session.module_preview_snapshot();
    if (!snapshot) {
        return std::unexpected(snapshot.error());
    }
    const auto environment = session.preview_environment();
    const toi::render::GrowthPreviewStageOptions options{
        .width = width,
        .height = height,
        .asset_search_path = environment.asset_search_path,
        .hdri_texture_path = environment.hdri_texture_path,
        .hdri_visible = environment.hdri_visible,
    };
    return toi::render::make_growth_preview_stage_projection(
        snapshot->snapshot, snapshot->camera_snapshot, snapshot->prepared_prototype, options);
}

} // namespace
#endif

int main(int argc, char* argv[])
{
#if defined(Q_OS_LINUX)
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("xcb"));
#endif
    if (QDir(QStringLiteral(TOI_QT_WEBENGINE_RESOURCES_PATH)).exists()) {
        qputenv("QTWEBENGINEPROCESS_PATH", QByteArrayLiteral(TOI_QT_WEBENGINE_PROCESS_PATH));
        qputenv("QTWEBENGINE_RESOURCES_PATH", QByteArrayLiteral(TOI_QT_WEBENGINE_RESOURCES_PATH));
        qputenv("QTWEBENGINE_LOCALES_PATH", QByteArrayLiteral(TOI_QT_WEBENGINE_LOCALES_PATH));
        qputenv("QML2_IMPORT_PATH", QByteArrayLiteral(TOI_QML_IMPORT_PATH));
    }

    QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    QtWebEngineQuick::initialize();

    QGuiApplication application(argc, argv);
    application.setApplicationName("Trees of Insanity");
    application.setOrganizationName("Trees of Insanity");

    QVulkanInstance vulkanInstance;
    vulkanInstance.setApiVersion(QVersionNumber(1, 2));
    QByteArrayList instanceExtensions = QQuickGraphicsConfiguration::preferredInstanceExtensions();
    for (const QByteArray extension : {
             QByteArrayLiteral(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME),
             QByteArrayLiteral(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME),
         }) {
        if (!instanceExtensions.contains(extension)) {
            instanceExtensions.append(extension);
        }
    }
    vulkanInstance.setExtensions(instanceExtensions);
    if (!vulkanInstance.create()) {
        qFatal("Failed to create Qt Vulkan instance");
    }

    qmlRegisterType<ViewportTextureItem>("TreesOfInsanity", 1, 0, "ViewportTextureItem");
    auto bridge = DesktopBridge::create(&application);
    std::unique_ptr<VulkanDevice> vulkanDevice;
#if defined(TOI_ENABLE_OVRTX)
    std::unique_ptr<toi::viewport::PreviewRenderer> previewRenderer;
#endif
    int exitCode = 1;
    {
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("desktopBridge", bridge.get());
        QString uiIndexPath = QDir(QCoreApplication::applicationDirPath())
                                  .absoluteFilePath(QStringLiteral("../share/trees-of-insanity/ui/index.html"));
        if (!QFileInfo::exists(uiIndexPath)) {
            uiIndexPath = QStringLiteral(TOI_UI_SOURCE_INDEX_PATH);
        }
        engine.rootContext()->setContextProperty("uiUrl", QUrl::fromLocalFile(uiIndexPath));
        engine.load(QUrl(QStringLiteral("qrc:/shell/main.qml")));
        if (engine.rootObjects().isEmpty()) {
            return 1;
        }

        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
        if (window == nullptr) {
            qFatal("Qt shell root is not a QQuickWindow");
        }
        try {
            vulkanDevice = VulkanDevice::create(*window, vulkanInstance, 0);
        } catch (const std::exception& error) {
            qFatal("Failed to initialize shared Qt/CUDA Vulkan device: %s", error.what());
        }
        auto* viewportItem = window->findChild<ViewportTextureItem*>(QStringLiteral("viewportTexture"));
        if (viewportItem == nullptr) {
            qFatal("Qt shell viewport texture item is missing");
        }
#if defined(TOI_ENABLE_OVRTX)
        auto initialStage = make_preview_projection(bridge->session());
        if (!initialStage) {
            qFatal("Failed to create initial preview projection: %s", initialStage.error().message.c_str());
        }
        auto createdRenderer = toi::viewport::PreviewRenderer::create(
            {
                .physical_device = vulkanDevice->physicalDevice(),
                .device = vulkanDevice->device(),
                .queue = vulkanDevice->queue(),
                .queue_family = vulkanDevice->queueFamily(),
                .cuda_device = 0,
                .device_name = vulkanDevice->name(),
            },
            std::move(*initialStage));
        if (!createdRenderer) {
            qFatal("Failed to create Qt preview renderer: %s", createdRenderer.error().message.c_str());
        }
        previewRenderer = std::move(*createdRenderer);
        const auto initialEnvironment = bridge->session().preview_environment();
        previewRenderer->set_guide_options(initialEnvironment.guides_visible,
                                           initialEnvironment.world_origin_axes_visible);
        viewportItem->setRenderer(previewRenderer.get());
        previewRenderer->set_frame_ready_callback([viewportItem] {
            QMetaObject::invokeMethod(viewportItem, [viewportItem] { viewportItem->update(); }, Qt::QueuedConnection);
        });
        QSize requestedExtent(previewRenderer->width(), previewRenderer->height());
        QSize pendingExtent = requestedExtent;
        QTimer resizeTimer;
        resizeTimer.setSingleShot(true);
        resizeTimer.setInterval(140);
        QObject::connect(&resizeTimer, &QTimer::timeout, viewportItem,
                         [bridge = bridge.get(), renderer = previewRenderer.get(),
                          &pendingExtent, &requestedExtent] {
                             if (pendingExtent == requestedExtent) return;
                             auto stage = make_preview_projection(
                                 bridge->session(), pendingExtent.width(), pendingExtent.height());
                             if (!stage) {
                                 bridge->publishViewportStatus(QString::fromStdString(
                                     std::string(R"({"phase":"error","message":")") +
                                     stage.error().message +
                                     R"(","viewport":{"width":0,"height":0},"color":{"width":0,"height":0},"depth":null,"frame_generation":0})"));
                                 return;
                             }
                             requestedExtent = pendingExtent;
                             renderer->set_stage(std::move(*stage));
                         });
        QObject::connect(bridge.get(), &DesktopBridge::viewportCameraInput, viewportItem,
                         [renderer = previewRenderer.get()](const QString& kind, double dx, double dy, int height) {
                             renderer->apply_camera_input(kind.toStdString(), static_cast<float>(dx),
                                                          static_cast<float>(dy), height);
                         });
        QObject::connect(bridge.get(), &DesktopBridge::previewInvalidated, viewportItem,
                         [bridge = bridge.get(), renderer = previewRenderer.get(), &requestedExtent] {
                             auto stage = make_preview_projection(
                                 bridge->session(), requestedExtent.width(), requestedExtent.height());
                             if (!stage) {
                                 bridge->publishViewportStatus(QString::fromStdString(
                                     std::string(R"({"phase":"error","message":")") +
                                     stage.error().message +
                                     R"(","viewport":{"width":0,"height":0},"color":{"width":0,"height":0},"depth":null,"frame_generation":0})"));
                                 return;
                             }
                             const auto environment = bridge->session().preview_environment();
                             renderer->set_guide_options(environment.guides_visible,
                                                         environment.world_origin_axes_visible);
                             renderer->set_stage(std::move(*stage));
                         });
        QTimer statusTimer;
        statusTimer.setInterval(250);
        QObject::connect(&statusTimer, &QTimer::timeout, bridge.get(),
                         [bridge = bridge.get(), renderer = previewRenderer.get()] {
                             const auto status = renderer->status();
                             bridge->publishViewportStatus(QString::fromStdString(
                                 std::string("{\"phase\":\"") + status.phase +
                                 "\",\"message\":\"" + status.message +
                                 "\",\"viewport\":{\"width\":" + std::to_string(status.width) +
                                 ",\"height\":" + std::to_string(status.height) +
                                 "},\"color\":{\"width\":" + std::to_string(status.width) +
                                 ",\"height\":" + std::to_string(status.height) +
                                 "},\"depth\":null,\"frame_generation\":" +
                                 std::to_string(status.frame_generation) + "}"));
                         });
        statusTimer.start();
        QObject::connect(bridge.get(), &DesktopBridge::viewportRectChanged, viewportItem,
                         [viewportItem, &resizeTimer, &pendingExtent](double x, double y, double width,
                                                                     double height, double devicePixelRatio) {
                             viewportItem->setPosition(QPointF(x, y));
                             viewportItem->setSize(QSizeF(width, height));
                             double pixelWidth = std::max(1.0, width * devicePixelRatio);
                             double pixelHeight = std::max(1.0, height * devicePixelRatio);
                             const double scale = std::min(1.0, 4096.0 / std::max(pixelWidth, pixelHeight));
                             pixelWidth *= scale;
                             pixelHeight *= scale;
                             pendingExtent = QSize(
                                 std::max(16, static_cast<int>(std::lround(pixelWidth))),
                                 std::max(16, static_cast<int>(std::lround(pixelHeight))));
                             resizeTimer.start();
                         });
#endif
        window->show();
        bridge->publishViewportStatus(QString::fromStdString(
            std::string(R"({"phase":"starting","message":"Qt Vulkan viewport starting on )") +
            vulkanDevice->name() +
            R"(","viewport":{"width":0,"height":0},"color":{"width":0,"height":0},"depth":null,"frame_generation":0})"));
        exitCode = application.exec();
    }
#if defined(TOI_ENABLE_OVRTX)
    previewRenderer.reset();
#endif
    vulkanDevice.reset();
    return exitCode;
}
