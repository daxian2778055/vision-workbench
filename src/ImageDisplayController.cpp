#include "ImageDisplayController.h"
#include "HalconWindow.h"
#include "NodeBase.h"
#include "DataObject.h"
#include "FlowScene.h"
#include "AppLog.h"

ImageDisplayController::ImageDisplayController(HalconWindow *view, QLabel *sourceLabel,
                                                 QObject *parent)
    : QObject(parent)
    , m_imageView(view)
    , m_sourceLabel(sourceLabel)
{
}

void ImageDisplayController::displayNodeImage(NodeBase *node)
{
    if (!node || !m_imageView) return;

    QSharedPointer<DataObject> outputData = node->getOutputData(0);
    if (outputData) {
        HalconCpp::HImage image = outputData->getHImage();
        if (image.IsInitialized()) {
            m_imageView->setImage(image, node->fullName());
            if (m_sourceLabel) {
                m_sourceLabel->setText(QStringLiteral("\u56FE\u50CF\u6765\u6E90: %1").arg(node->fullName()));
            }
        }
    }
}

NodeBase *ImageDisplayController::resolveDisplayNode(NodeBase *fallbackNode,
                                                       FlowScene *currentScene) const
{
    if (!currentScene) {
        return fallbackNode;
    }

    // Rule 1: User explicitly selected output node via dropdown
    if (m_selectedOutputNodes.contains(currentScene)) {
        NodeBase *selected = m_selectedOutputNodes.value(currentScene);
        if (selected && selected->hasExecuted()) {
            return selected;
        }
    }

    // Rule 2: Currently selected node on canvas
    // (This will be checked by the caller through scene->selectedNodes())

    // Rule 3: Fallback to the most recently executed node
    return fallbackNode;
}

void ImageDisplayController::setSelectedOutputNode(FlowScene *scene, NodeBase *node)
{
    if (scene) {
        m_selectedOutputNodes[scene] = node;
    }
}

NodeBase *ImageDisplayController::selectedOutputNode(FlowScene *scene) const
{
    return m_selectedOutputNodes.value(scene, nullptr);
}

void ImageDisplayController::clearSelection(FlowScene *scene)
{
    m_selectedOutputNodes.remove(scene);
}
