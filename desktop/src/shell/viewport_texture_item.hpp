#pragma once

#include <QQuickItem>

class VulkanDevice;
namespace toi::viewport {
class PreviewRenderer;
}

class ViewportTextureItem : public QQuickItem {
    Q_OBJECT

public:
    explicit ViewportTextureItem(QQuickItem* parent = nullptr);

    void setDevice(VulkanDevice* device);
    void setRenderer(toi::viewport::PreviewRenderer* renderer);

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

private:
    VulkanDevice* device_ = nullptr;
    toi::viewport::PreviewRenderer* renderer_ = nullptr;
};
