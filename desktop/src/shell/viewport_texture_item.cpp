#include "viewport_texture_item.hpp"

#include "toi/viewport/preview_renderer.hpp"

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
    if (renderer_ == nullptr || window() == nullptr || window()->rhi() == nullptr) {
        delete oldNode;
        return nullptr;
    }

    (void)renderer_->prepare_frame_on_render_thread();
    const VkImage image = renderer_->display_image();
    const VkImageLayout layout = renderer_->display_layout();
    const int width = renderer_->width();
    const int height = renderer_->height();

    auto* node = static_cast<ImportedTextureNode*>(oldNode);
    if (node == nullptr) {
        QRhi* rhi = window()->rhi();
        auto* rhiTexture = rhi->newTexture(
            QRhiTexture::RGBA8, QSize(renderer_->texture_width(), renderer_->texture_height()));
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
    node->setSourceRect(QRectF(0.0, 0.0, width, height));
    QRectF target = boundingRect();
    if (width > 0 && height > 0 && target.width() > 0.0 && target.height() > 0.0) {
        const double texture_aspect = static_cast<double>(width) / static_cast<double>(height);
        const double item_aspect = target.width() / target.height();
        if (item_aspect > texture_aspect) {
            const double fitted_width = target.height() * texture_aspect;
            target.setLeft(target.left() + (target.width() - fitted_width) * 0.5);
            target.setWidth(fitted_width);
        } else {
            const double fitted_height = target.width() / texture_aspect;
            target.setTop(target.top() + (target.height() - fitted_height) * 0.5);
            target.setHeight(fitted_height);
        }
    }
    node->setRect(target);
    return node;
}
