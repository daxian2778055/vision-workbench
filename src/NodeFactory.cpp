#include "NodeFactory.h"
#include "NodeRegistry.h"
#include "ImageReadNode.h"
#include "HalconNode.h"
#include "AppLog.h"

NodeBase *NodeFactory::tryCreateNamedTool(QObject *parent, const QString &name)
{
    registerAllNodes();
    const QString n = name.trimmed();

    // 特殊节点：失败时回退到通用 HalconNode（避免崩溃）
    if (n == QStringLiteral("MVS图像源")) {
        try {
            if (auto *node = NodeRegistry::instance().createByName(n, parent))
                return node;
        } catch (const std::exception &e) {
            VFP_DEBUG << "Exception creating MvsImageSourceNode:" << e.what();
        } catch (...) {
            VFP_DEBUG << "Unknown exception creating MvsImageSourceNode";
        }
        auto *node = new HalconNode(parent);
        node->setName(QStringLiteral("MVS图像源"));
        return node;
    }

    // 走声明式注册表创建（中文名/英文名均可）
    try {
        if (auto *node = NodeRegistry::instance().createByName(n, parent))
            return node;
    } catch (const std::exception &e) {
        VFP_DEBUG << "Exception creating node:" << n << e.what();
        return nullptr;
    } catch (...) {
        VFP_DEBUG << "Unknown exception creating node:" << n;
        return nullptr;
    }
    return nullptr;
}

QString NodeFactory::toolPaletteMimeFormat()
{
    return QStringLiteral("application/x-vfp-tool-palette-id");
}

bool NodeFactory::resolvePaletteToolId(const QString &paletteId,
                                       NodeBase::NodeType &outCategory, QString &outNodeName)
{
    registerAllNodes();
    const QString id = paletteId.trimmed();
    if (id.isEmpty())
        return false;

    const NodeRegistration *reg = NodeRegistry::instance().findById(id);
    if (reg) {
        outCategory = reg->category;
        outNodeName = reg->displayName;
        return true;
    }
    return false;
}

NodeBase *NodeFactory::createDefaultForCategory(QObject *parent, NodeBase::NodeType category)
{
    switch (category) {
    case NodeBase::IMAGE_ACQUISITION: {
        auto *node = new ImageReadNode(parent);
        node->setName(QStringLiteral("读取图像"));
        return node;
    }
    case NodeBase::IMAGE_PROCESSING: {
        auto *node = new HalconNode(parent);
        node->setName(QStringLiteral("图像处理"));
        return node;
    }
    case NodeBase::SHAPE_ANALYSIS: {
        auto *node = new HalconNode(parent);
        node->setName(QStringLiteral("形状分析"));
        return node;
    }
    case NodeBase::MEASUREMENT: {
        auto *node = new HalconNode(parent);
        node->setName(QStringLiteral("测量"));
        return node;
    }
    case NodeBase::LOGIC: {
        auto *node = new HalconNode(parent);
        node->setName(QStringLiteral("逻辑"));
        return node;
    }
    case NodeBase::OUTPUT: {
        auto *node = new HalconNode(parent);
        node->setName(QStringLiteral("输出"));
        return node;
    }
    }
    return nullptr;
}

NodeBase *NodeFactory::createNode(QObject *parent, NodeBase::NodeType category, const QString &nodeName)
{
    if (NodeBase *specific = tryCreateNamedTool(parent, nodeName))
        return specific;
    return createDefaultForCategory(parent, category);
}

bool NodeFactory::parseDropMimeText(const QString &typeStr, NodeBase::NodeType &type, QString &nodeName)
{
    nodeName = typeStr;

    if (typeStr == QStringLiteral("图像采集") || typeStr == QStringLiteral("Image Acquisition")) {
        type = NodeBase::IMAGE_ACQUISITION;
        return true;
    }
    if (typeStr == QStringLiteral("图像处理") || typeStr == QStringLiteral("Image Processing")) {
        type = NodeBase::IMAGE_PROCESSING;
        return true;
    }
    if (typeStr == QStringLiteral("形状分析") || typeStr == QStringLiteral("Shape Analysis")) {
        type = NodeBase::SHAPE_ANALYSIS;
        return true;
    }
    if (typeStr == QStringLiteral("测量") || typeStr == QStringLiteral("Measurement")) {
        type = NodeBase::MEASUREMENT;
        return true;
    }
    if (typeStr == QStringLiteral("逻辑") || typeStr == QStringLiteral("Logic")) {
        type = NodeBase::LOGIC;
        return true;
    }
    if (typeStr == QStringLiteral("输出") || typeStr == QStringLiteral("Output")) {
        type = NodeBase::OUTPUT;
        return true;
    }

    // 具体工具名：优先从注册表解析
    registerAllNodes();
    if (const NodeRegistration *reg = NodeRegistry::instance().findByName(typeStr)) {
        type = reg->category;
        nodeName = reg->displayName;
        return true;
    }

    return false;
}
