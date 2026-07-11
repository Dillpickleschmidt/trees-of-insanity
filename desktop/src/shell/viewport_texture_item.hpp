#pragma once

#include <QQuickItem>

namespace toi::viewport {
class PreviewRenderer;
}

class ViewportTextureItem : public QQuickItem {
    Q_OBJECT

public:
    explicit ViewportTextureItem(QQuickItem* parent = nullptr);

    void setRenderer(toi::viewport::PreviewRenderer* renderer);

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

private:
    toi::viewport::PreviewRenderer* renderer_ = nullptr;
};
