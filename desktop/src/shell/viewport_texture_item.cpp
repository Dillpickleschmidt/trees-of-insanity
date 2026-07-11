#include "viewport_texture_item.hpp"

#include "vulkan_device.hpp"
#if defined(TOI_ENABLE_OVRTX)
#include "toi/viewport/preview_renderer.hpp"
#endif

#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <rhi/qrhi.h>

#include <cstdint>

namespace {

class ImportedTextureNode final : public QSGSimpleTextureNode {
public:
    ImportedTextureNode(QRhiTexture* rhiTexture, QSGTexture* texture)
        : rhiTexture_(rhiTexture), texture_(texture)
    {
        setTexture(texture_);
        setOwnsTexture(false);
        setFiltering(QSGTexture::Linear);
    }

    ~ImportedTextureNode() override
    {
        delete texture_;
        delete rhiTexture_;
    }

private:
    QRhiTexture* rhiTexture_ = nullptr;
    QSGTexture* texture_ = nullptr;
};

} // namespace

ViewportTextureItem::ViewportTextureItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

void ViewportTextureItem::setDevice(VulkanDevice* device)
{
    if (device_ == device) {
        return;
    }
    device_ = device;
    update();
}

void ViewportTextureItem::setRenderer(toi::viewport::PreviewRenderer* renderer)
{
    if (renderer_ == renderer) {
        return;
    }
    renderer_ = renderer;
    update();
}

QSGNode* ViewportTextureItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    if (device_ == nullptr || window() == nullptr || window()->rhi() == nullptr) {
        delete oldNode;
        return nullptr;
    }

    VkImage image = device_->previewImage();
    VkImageLayout layout = device_->previewImageLayout();
    int width = static_cast<int>(device_->previewWidth());
    int height = static_cast<int>(device_->previewHeight());
#if defined(TOI_ENABLE_OVRTX)
    if (renderer_ != nullptr) {
        (void)renderer_->prepare_frame_on_render_thread();
        image = renderer_->display_image();
        layout = renderer_->display_layout();
        width = renderer_->width();
        height = renderer_->height();
    }
#endif

    auto* node = static_cast<ImportedTextureNode*>(oldNode);
    if (node == nullptr) {
        QRhi* rhi = window()->rhi();
        auto* rhiTexture = rhi->newTexture(QRhiTexture::RGBA8, QSize(width, height));
        const QRhiTexture::NativeTexture native{
            static_cast<quint64>(reinterpret_cast<std::uintptr_t>(image)),
            static_cast<int>(layout),
        };
        if (!rhiTexture->createFrom(native)) {
            delete rhiTexture;
            return nullptr;
        }
        QSGTexture* texture = window()->createTextureFromRhiTexture(rhiTexture);
        if (texture == nullptr) {
            delete rhiTexture;
            return nullptr;
        }
        node = new ImportedTextureNode(rhiTexture, texture);
    }
    node->setRect(boundingRect());
    return node;
}
