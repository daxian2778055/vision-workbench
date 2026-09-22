#include "NodeRegistry.h"
#include "ImageReadNode.h"
#include "MvsImageSourceNode.h"
#include "HalconImageSourceNode.h"
#include "ColorConversionNode.h"
#include "ThresholdNode.h"
#include "BlurNode.h"
#include "DilateNode.h"
#include "ErodeNode.h"
#include "OpenNode.h"
#include "CloseNode.h"
#include "TopHatNode.h"
#include "BottomHatNode.h"
#include "RotateImageNode.h"
#include "TranslateImageNode.h"
#include "ResizeNode.h"
#include "MirrorNode.h"
#include "HistogramEqualizeNode.h"
#include "ContrastStretchNode.h"
#include "SharpenNode.h"
#include "GrayStretchNode.h"
#include "MedianFilterNode.h"
#include "ImageAddNode.h"
#include "ImageSubNode.h"
#include "ImageMulNode.h"
#include "ImageDivNode.h"
#include "ImageInvertNode.h"
#include "OtsuThresholdNode.h"
#include "DynThresholdNode.h"
#include "CropNode.h"
#include "ConvertImageNode.h"
#include "PixelStatsNode.h"
#include "CornerNode.h"
#include "EdgePointsNode.h"
#include "CaliperMeasureNode.h"
#include "FindLineNode.h"
#include "FindCircleNode.h"
#include "AngleMeasureNode.h"
#include "PointLineDistanceNode.h"
#include "PointCircleDistanceNode.h"
#include "DistanceMeasureNode.h"
#include "PositionCorrectNode.h"
#include "CoordinateTransformNode.h"
#include "DataCode2DNode.h"
#include "NPointCalibNode.h"
#include "CalibrationBoardNode.h"
#include "HandEyeCalibNode.h"
#include "BlobAnalysisNode.h"
#include "EdgeDetectionNode.h"
#include "TemplateMatchNode.h"
#include "CalibrationNode.h"
#include "OcrNode.h"
#include "BarcodeNode.h"
#include "DistanceNode.h"
#include "AreaNode.h"
#include "FitLineNode.h"
#include "FitCircleNode.h"
#include "ConditionalNode.h"
#include "LoopNode.h"
#include "DisplaySinkNode.h"
#include "WriteFileNode.h"
#include "ScriptNode.h"
#include "ReceiveDataNode.h"
#include "SendDataNode.h"
#include "ProtocolParseNode.h"
#include "FormatNode.h"
#include "FormulaNode.h"
#include "DelayNode.h"
#include "CounterNode.h"
#include "RecordNode.h"
#include "FilterNode.h"
#include "ClassifyNode.h"
#include "SortNode.h"
#include "OpencvThresholdNode.h"
#include "OpencvAdaptiveThresholdNode.h"
#include "OpencvAngleNode.h"
#include "DnnDetectNode.h"
#include "DnnSegmentNode.h"
#include "CameraIoNode.h"
#include "OpencvCropNode.h"
#include "OpencvImageArithNode.h"
#include "OpencvRotateNode.h"
#include "OpencvPixelStatsNode.h"
#include "OpencvBlobNode.h"
#include "OpencvMorphNode.h"
#include "OpencvEdgeNode.h"
#include "OpencvFitLineNode.h"
#include "OpencvFitCircleNode.h"
#include "OpencvTemplateMatchNode.h"
#include "OpencvDefectNode.h"
#include "OpencvCaliperNode.h"
#include "OpencvTrainClassifierNode.h"
#include "OpencvClassifyNode.h"
#include "DnnInferNode.h"
#include "DeepOcrNode.h"
#include "OpencvTrackNode.h"
#include "OpencvCalibNode.h"
#include "OpencvQrNode.h"
#include "ZxingBarcodeNode.h"
#include "TesseractOcrNode.h"

// 简洁注册宏：id=调色板ID，ch/en=中英文名，cat=分类，grp=工具库分组，T=工厂类
#define VFP_REG(T, id, ch, en, cat, grp) \
    r.registerNode({ QStringLiteral(id), QStringLiteral(ch), QStringLiteral(en), \
                     cat, QStringLiteral(grp), [](QObject *p) -> NodeBase * { return new T(p); }, QString() })

void registerAllNodes()
{
    auto &r = NodeRegistry::instance();

    // ===== 图像采集 =====
    VFP_REG(ImageReadNode,        "ImageReadNode",        "\u8BFB\u53D6\u56FE\u50CF", "Read Image",
            NodeBase::IMAGE_ACQUISITION, "\u56FE\u50CF\u91C7\u96C6");
    VFP_REG(MvsImageSourceNode,   "MvsImageSourceNode",   "MVS\u56FE\u50CF\u6E90", "MVS Source",
            NodeBase::IMAGE_ACQUISITION, "\u56FE\u50CF\u91C7\u96C6");
    VFP_REG(HalconImageSourceNode,"HalconImageSourceNode","Halcon\u56FE\u50CF\u6E90", "Halcon Source",
            NodeBase::IMAGE_ACQUISITION, "\u56FE\u50CF\u91C7\u96C6");

    // ===== 图像处理 =====
    VFP_REG(ColorConversionNode,  "ColorConversionNode",  "\u989C\u8272\u8F6C\u6362", "Color Conversion",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    // ThresholdNode 已移除注册：替换版环境区域算子有跨进程反转实证（另一进程阈值面积 31.5 错误）
    // VFP_REG(ThresholdNode,        "ThresholdNode",        "\u9608\u503C", "Threshold",
    //         NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    // 替代：OpencvThresholdNode（固定阈值/OTSU 模式）
    VFP_REG(BlurNode,             "BlurNode",             "\u6A21\u7CCA", "Blur",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");

    // ===== 图像处理：形态学 =====
    // 形态学 6 节点已移除注册：区域算子跨进程反转风险（替代：OpencvMorphNode）
    // VFP_REG(DilateNode,           "DilateNode",           "\u81A8\u80C0", "Dilate",
    //         NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    // VFP_REG(ErodeNode,            "ErodeNode",            "\u8150\u8680", "Erode",
    //         NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    // VFP_REG(OpenNode,             "OpenNode",             "\u5F00\u8FD0\u7B97", "Open",
    //         NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    // VFP_REG(CloseNode,            "CloseNode",            "\u95ED\u8FD0\u7B97", "Close",
    //         NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    // VFP_REG(TopHatNode,           "TopHatNode",           "\u9876\u5E3D\u53D8\u6362", "Top Hat",
    //         NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    // VFP_REG(BottomHatNode,        "BottomHatNode",        "\u5E95\u5E3D\u53D8\u6362", "Bottom Hat",
    //         NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");

    // ===== 图像处理：仿射变换 =====
    // RotateImageNode 已移除注册：替换版环境 RotateImage 数值级验证垃圾输出（替代：OpencvRotateNode）
    // VFP_REG(RotateImageNode,      "RotateImageNode",      "\u56FE\u50CF\u65CB\u8F6C", "Rotate Image",
    //         NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    VFP_REG(TranslateImageNode,   "TranslateImageNode",   "\u56FE\u50CF\u5E73\u79FB", "Translate Image",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    VFP_REG(ResizeNode,           "ResizeNode",           "\u56FE\u50CF\u7F29\u653E", "Resize",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    VFP_REG(MirrorNode,           "MirrorNode",           "\u56FE\u50CF\u955C\u50CF", "Mirror",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");

    // ===== 图像处理：增强 =====
    VFP_REG(HistogramEqualizeNode,"HistogramEqualizeNode","\u76F4\u65B9\u56FE\u5747\u8861", "Histogram Equalize",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    VFP_REG(ContrastStretchNode,  "ContrastStretchNode",  "\u5BF9\u6BD4\u5EA6\u62C9\u4F38", "Contrast Stretch",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    VFP_REG(SharpenNode,          "SharpenNode",          "\u56FE\u50CF\u9510\u5316", "Sharpen",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    VFP_REG(GrayStretchNode,      "GrayStretchNode",      "\u7070\u5EA6\u62C9\u4F38", "Gray Stretch",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    VFP_REG(MedianFilterNode,     "MedianFilterNode",     "\u4E2D\u503C\u6EE4\u6CE2", "Median Filter",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");

    // ===== 图像处理：运算 =====
    // ImageAddNode/ImageMulNode 已移除注册：AddImage/MultImage 数值级验证错误（替代：Opencv图像运算）
    // VFP_REG(ImageAddNode,         "ImageAddNode",         "\u56FE\u50CF\u52A0\u6CD5", "Add Image",
    //         NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    // VFP_REG(ImageMulNode,         "ImageMulNode",         "\u56FE\u50CF\u4E58\u6CD5", "Multiply Image",
    //         NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    // ImageSub/Div 已改为 OpenCV subtract/divide（避免常数路径走 AddImage）
    VFP_REG(ImageSubNode,         "ImageSubNode",         "\u56FE\u50CF\u51CF\u6CD5", "Sub Image",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    VFP_REG(ImageDivNode,         "ImageDivNode",         "\u56FE\u50CF\u9664\u6CD5", "Divide Image",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    VFP_REG(ImageInvertNode,      "ImageInvertNode",      "\u56FE\u50CF\u6C42\u53CD", "Invert Image",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");

    // ===== 图像处理：二值化/裁剪/转换 =====
    // OtsuThresholdNode 已移除注册：区域算子跨进程反转风险（替代：OpencvThresholdNode OTSU 模式）
    // VFP_REG(OtsuThresholdNode,    "OtsuThresholdNode",    "Otsu\u4E8C\u503C\u5316", "Otsu Threshold",
    //         NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");

    // ===== 图像算法（OpenCV；旧方案名「OpenCV*」见下方 aliases）=====
    VFP_REG(OpencvThresholdNode,  "OpencvThresholdNode",  "二值化", "Threshold",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    VFP_REG(OpencvAdaptiveThresholdNode, "OpencvAdaptiveThresholdNode",
            "自适应阈值", "Adaptive Threshold",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    VFP_REG(OpencvCropNode,       "OpencvCropNode",       "ROI裁剪", "ROI Crop",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    VFP_REG(OpencvImageArithNode, "OpencvImageArithNode", "图像运算", "Image Arith",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    VFP_REG(OpencvRotateNode,     "OpencvRotateNode",     "图像旋转", "Rotate",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    VFP_REG(OpencvPixelStatsNode, "OpencvPixelStatsNode", "灰度统计", "Pixel Stats",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    VFP_REG(OpencvBlobNode,       "OpencvBlobNode",       "Blob分析", "Blob",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    VFP_REG(OpencvMorphNode,      "OpencvMorphNode",      "形态学", "Morphology",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    VFP_REG(OpencvEdgeNode,       "OpencvEdgeNode",       "边缘检测", "Edge",
            NodeBase::SHAPE_ANALYSIS, "\u7279\u5F81\u5B9A\u4F4D");
    VFP_REG(OpencvFitLineNode,    "OpencvFitLineNode",    "直线拟合", "Fit Line",
            NodeBase::SHAPE_ANALYSIS, "\u51E0\u4F55\u6D4B\u91CF");
    VFP_REG(OpencvFitCircleNode,  "OpencvFitCircleNode",  "圆拟合", "Fit Circle",
            NodeBase::SHAPE_ANALYSIS, "\u51E0\u4F55\u6D4B\u91CF");
    VFP_REG(OpencvTemplateMatchNode,"OpencvTemplateMatchNode", "模板匹配", "Template Match",
            NodeBase::SHAPE_ANALYSIS, "\u7279\u5F81\u5B9A\u4F4D");
    VFP_REG(OpencvDefectNode,     "OpencvDefectNode",     "缺陷检测", "Defect",
            NodeBase::SHAPE_ANALYSIS, "\u7279\u5F81\u5B9A\u4F4D");
    VFP_REG(OpencvCaliperNode,    "OpencvCaliperNode",    "卡尺测量", "Caliper",
            NodeBase::SHAPE_ANALYSIS, "\u51E0\u4F55\u6D4B\u91CF");
    // 🥇 角度测量：替代替换版环境下被禁用的 HALCON AngleMeasureNode（纯几何，无 HALCON 区域链路）
    VFP_REG(OpencvAngleNode,      "OpencvAngleNode",      "角度测量", "Angle Measure",
            NodeBase::MEASUREMENT, "\u51E0\u4F55\u6D4B\u91CF");
    VFP_REG(OpencvTrainClassifierNode, "OpencvTrainClassifierNode", "分类器训练", "Classifier Train",
            NodeBase::IMAGE_PROCESSING, "\u6DF1\u5EA6\u5B66\u4E60");
    VFP_REG(OpencvClassifyNode,       "OpencvClassifyNode",       "分类推理", "Classify",
            NodeBase::IMAGE_PROCESSING, "\u6DF1\u5EA6\u5B66\u4E60");
    VFP_REG(DnnInferNode,             "DnnInferNode",             "ONNX深度学习推理", "ONNX DNN Inference",
            NodeBase::IMAGE_PROCESSING, "\u6DF1\u5EA6\u5B66\u4E60");
    // 🥈 目标检测：替代替换版环境下被禁用的 HALCON 区域检测（FR13.3 / G-P0-1）
    VFP_REG(DnnDetectNode,            "DnnDetectNode",            "ONNX目标检测", "ONNX DNN Detect",
            NodeBase::IMAGE_PROCESSING, "\u6DF1\u5EA6\u5B66\u4E60");
    // 🥇 分割：像素级缺陷区域输出（FR13.4 / G-P0-2），检测节点的下一环
    VFP_REG(DnnSegmentNode,           "DnnSegmentNode",           "ONNX分割", "ONNX DNN Segment",
            NodeBase::IMAGE_PROCESSING, "\u6DF1\u5EA6\u5B66\u4E60");
    // 🥉 相机 IO 控制：对标 VM 4.4 IO 控制（FR16.12），收敛为"仅相机 IO"
    // （经 GlobalCameraManager 走海康 MVS GenICam LineSelector/LineMode/LineStatus）
    VFP_REG(CameraIoNode,             "CameraIoNode",             "相机IO控制", "Camera IO Control",
            NodeBase::OUTPUT, "\u76F8\u673A IO");
    VFP_REG(OpencvCalibNode,      "OpencvCalibNode",      "相机标定", "Camera Calibration",
            NodeBase::SHAPE_ANALYSIS, "\u6807\u5B9A");
    VFP_REG(OpencvQrNode,         "OpencvQrNode",         "二维码解码", "QR Decode",
            NodeBase::SHAPE_ANALYSIS, "\u8BC6\u522B");
    VFP_REG(ZxingBarcodeNode,     "ZxingBarcodeNode",     "ZXing条码解码", "ZXing Barcode",
            NodeBase::SHAPE_ANALYSIS, "\u8BC6\u522B");
    VFP_REG(TesseractOcrNode,     "TesseractOcrNode",     "TesseractOCR", "Tesseract OCR",
            NodeBase::SHAPE_ANALYSIS, "\u8BC6\u522B");
    VFP_REG(DeepOcrNode,          "DeepOcrNode",          "HALCON DeepOCR", "HALCON Deep OCR",
            NodeBase::SHAPE_ANALYSIS, "\u8BC6\u522B");
    VFP_REG(OpencvTrackNode,      "OpencvTrackNode",      "目标跟踪", "Object Track",
            NodeBase::IMAGE_PROCESSING, "\u8DDF\u8E2A");
    // DynThresholdNode 已移除注册：区域算子跨进程反转风险（替代：OpencvAdaptiveThresholdNode）
    // VFP_REG(DynThresholdNode,     "DynThresholdNode",     "\u52A8\u6001\u9608\u503C", "Dynamic Threshold",
    //         NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    // CropNode 已移除注册：区域算子跨进程反转风险（替代：OpencvCropNode）
    // VFP_REG(CropNode,             "CropNode",             "ROI\u88C1\u526A", "ROI Crop",
    //         NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    VFP_REG(ConvertImageNode,     "ConvertImageNode",     "\u7C7B\u578B\u8F6C\u6362", "Convert Type",
            NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");
    // PixelStatsNode 已移除注册：MinMaxGray 数值级验证垃圾输出（替代：Opencv灰度统计）
    // VFP_REG(PixelStatsNode,       "PixelStatsNode",       "\u7070\u5EA6\u7EDF\u8BA1", "Pixel Stats",
    //         NodeBase::IMAGE_PROCESSING, "\u56FE\u50CF\u5904\u7406");

    // ===== 特征定位 =====
    // BlobAnalysisNode 已移除注册：替换版环境区域几何特征数据错误（SmallestRectangle2 宽=0.78 实测）
    // VFP_REG(BlobAnalysisNode,     "BlobAnalysisNode",     "Blob\u5206\u6790", "Blob Analysis",
    //         NodeBase::SHAPE_ANALYSIS, "\u7279\u5F81\u5B9A\u4F4D");
    // 替代：OpencvBlobNode
    // EdgeDetectionNode 已移除注册：SobelAmp 数值级验证垃圾输出（替代：OpencvEdgeNode）
    // VFP_REG(EdgeDetectionNode,    "EdgeDetectionNode",    "\u8FB9\u7F18\u68C0\u6D4B", "Edge Detection",
    //         NodeBase::SHAPE_ANALYSIS, "\u7279\u5F81\u5B9A\u4F4D");
    // TemplateMatchNode 已移除注册：替换版环境 CreateShapeModel #1402 实测异常
    // VFP_REG(TemplateMatchNode,    "TemplateMatchNode",    "\u6A21\u677F\u5339\u914D", "Template Match",
    //         NodeBase::SHAPE_ANALYSIS, "\u7279\u5F81\u5B9A\u4F4D");
    // 替代：OpencvTemplateMatchNode
    // CalibrationNode 已移除注册：替换版环境 FindCalibObject #8403（描述文件损坏）实测异常
    // VFP_REG(CalibrationNode,      "CalibrationNode",      "\u76F8\u673A\u6807\u5B9A", "Calibration",
    //         NodeBase::SHAPE_ANALYSIS, "\u7279\u5F81\u5B9A\u4F4D");
    // 替代：OpencvCalibNode（需 OpenCV 4.8+）
    // OCR/条码识别节点已移除注册（替换版环境下 OCR 抛 #2404、条码区域损坏，
    // 均无 OpenCV 替代；代码保留在 src/，正常 HALCON 环境可恢复注册）
    // VFP_REG(OcrNode,          "OcrNode",          "OCR\u8BC6\u522B", "OCR",
    //         NodeBase::SHAPE_ANALYSIS, "\u7279\u5F81\u5B9A\u4F4D");
    // VFP_REG(BarcodeNode,      "BarcodeNode",      "\u6761\u7801\u8BC6\u522B", "Barcode",
    //         NodeBase::SHAPE_ANALYSIS, "\u7279\u5F81\u5B9A\u4F4D");

    // ===== 特征定位：Phase2 扩展 =====
    VFP_REG(CornerNode,           "CornerNode",           "\u89D2\u70B9\u68C0\u6D4B", "Corner Detect",
            NodeBase::SHAPE_ANALYSIS, "\u7279\u5F81\u5B9A\u4F4D");
    // EdgePointsNode 已移除注册：替换版环境 ThresholdSubPix 实测 Index out of range
    // VFP_REG(EdgePointsNode,       "EdgePointsNode",       "\u4E9A\u50CF\u7D20\u8FB9\u7F18\u70B9", "Edge Points",
    //         NodeBase::SHAPE_ANALYSIS, "\u7279\u5F81\u5B9A\u4F4D");
    // 替代：OpencvEdgeNode

    // ===== 几何测量：Phase2 扩展 =====
    // 测量类四节点已移除注册：替换版环境 Metrology 实测 Index out of range
    // 替代：OpencvCaliperNode / OpencvFitLineNode / OpencvFitCircleNode
    // VFP_REG(CaliperMeasureNode,   "CaliperMeasureNode",   "\u5361\u5C3A\u6D4B\u91CF", "Caliper",
    //         NodeBase::MEASUREMENT, "\u51E0\u4F55\u6D4B\u91CF");
    // VFP_REG(FindLineNode,         "FindLineNode",         "\u7EBF\u6BB5\u67E5\u627E", "Find Line",
    //         NodeBase::MEASUREMENT, "\u51E0\u4F55\u6D4B\u91CF");
    // VFP_REG(FindCircleNode,       "FindCircleNode",       "\u5706\u67E5\u627E", "Find Circle",
    //         NodeBase::MEASUREMENT, "\u51E0\u4F55\u6D4B\u91CF");
    // VFP_REG(AngleMeasureNode,     "AngleMeasureNode",     "\u89D2\u5EA6\u6D4B\u91CF", "Angle Measure",
    //         NodeBase::MEASUREMENT, "\u51E0\u4F55\u6D4B\u91CF");
    VFP_REG(PointLineDistanceNode,"PointLineDistanceNode","\u70B9\u7EBF\u8DDD\u79BB", "Point-Line Distance",
            NodeBase::MEASUREMENT, "\u51E0\u4F55\u6D4B\u91CF");
    VFP_REG(PointCircleDistanceNode, "PointCircleDistanceNode", "\u70B9\u5706\u8DDD\u79BB", "Point-Circle Distance",
            NodeBase::MEASUREMENT, "\u51E0\u4F55\u6D4B\u91CF");
    VFP_REG(DistanceMeasureNode,  "DistanceMeasureNode",  "\u4E24\u70B9\u8DDD\u79BB", "Distance PP",
            NodeBase::MEASUREMENT, "\u51E0\u4F55\u6D4B\u91CF");
    VFP_REG(PositionCorrectNode,  "PositionCorrectNode",  "\u4F4D\u7F6E\u4FEE\u6B63", "Position Correct",
            NodeBase::MEASUREMENT, "\u51E0\u4F55\u6D4B\u91CF");
    VFP_REG(CoordinateTransformNode, "CoordinateTransformNode", "\u5750\u6807\u7CFB\u53D8\u6362", "Coordinate Transform",
            NodeBase::MEASUREMENT, "\u51E0\u4F55\u6D4B\u91CF");

    // ===== 识别与标定：Phase3 =====
    // 二维码识别节点已移除注册（替换版下区域损坏不可靠，已由 OpenCV二维码解码节点替代）
    // VFP_REG(DataCode2DNode,   "DataCode2DNode",   "\u4E8C\u7EF4\u7801\u8BC6\u522B", "2D Code",
    //         NodeBase::SHAPE_ANALYSIS, "\u7279\u5F81\u5B9A\u4F4D");
    VFP_REG(NPointCalibNode,      "NPointCalibNode",      "N\u70B9\u6807\u5B9A", "N-Point Calib",
            NodeBase::SHAPE_ANALYSIS, "\u7279\u5F81\u5B9A\u4F4D");
    // CalibrationBoardNode 已移除注册：替换版环境 FindCalibObject #8403（描述文件损坏）实测异常
    // VFP_REG(CalibrationBoardNode, "CalibrationBoardNode", "\u6807\u5B9A\u677F\u6807\u5B9A", "Calib Board",
    //         NodeBase::SHAPE_ANALYSIS, "\u7279\u5F81\u5B9A\u4F4D");
    // 替代：OpencvCalibNode（需 OpenCV 4.8+）
    VFP_REG(HandEyeCalibNode,     "HandEyeCalibNode",     "\u624B\u773C\u6807\u5B9A", "Hand-Eye Calib",
            NodeBase::SHAPE_ANALYSIS, "\u7279\u5F81\u5B9A\u4F4D");

    // ===== 几何测量 =====
    VFP_REG(DistanceNode,         "DistanceNode",         "\u8DDD\u79BB", "Distance",
            NodeBase::MEASUREMENT, "\u51E0\u4F55\u6D4B\u91CF");
    // AreaNode 已移除注册：区域算子跨进程反转风险（替代：OpencvBlobNode 输出面积端口）
    // VFP_REG(AreaNode,             "AreaNode",             "\u9762\u79EF", "Area",
    //         NodeBase::MEASUREMENT, "\u51E0\u4F55\u6D4B\u91CF");
    // FitLineNode/FitCircleNode 已移除注册：内部依赖区域算子（Threshold+SelectShape），区域跨进程反转风险（替代：OpencvFitLineNode/OpencvFitCircleNode）
    // VFP_REG(FitLineNode,          "FitLineNode",          "\u76F4\u7EBF\u62DF\u5408", "Fit Line",
    //         NodeBase::MEASUREMENT, "\u51E0\u4F55\u6D4B\u91CF");
    // VFP_REG(FitCircleNode,        "FitCircleNode",        "\u5706\u62DF\u5408", "Fit Circle",
    //         NodeBase::MEASUREMENT, "\u51E0\u4F55\u6D4B\u91CF");

    // ===== 逻辑与控制 =====
    VFP_REG(ConditionalNode,      "ConditionNode",        "\u6761\u4EF6\u5224\u65AD", "If-Else",
            NodeBase::LOGIC, "\u903B\u8F91\u4E0E\u63A7\u5236");
    VFP_REG(LoopNode,             "LoopNode",             "\u5FAA\u73AF", "Loop",
            NodeBase::LOGIC, "\u903B\u8F91\u4E0E\u63A7\u5236");

    // ===== 结果输出 =====
    VFP_REG(DisplaySinkNode,      "DisplayNode",          "\u663E\u793A", "Display",
            NodeBase::OUTPUT, "\u7ED3\u679C\u8F93\u51FA");
    VFP_REG(WriteFileNode,        "WriteFileNode",        "\u5199\u5165\u6587\u4EF6", "Write File",
            NodeBase::OUTPUT, "\u7ED3\u679C\u8F93\u51FA");
    VFP_REG(ScriptNode,           "ScriptNode",           "\u811A\u672C\u6267\u884C", "Script",
            NodeBase::OUTPUT, "\u7ED3\u679C\u8F93\u51FA");
    VFP_REG(ReceiveDataNode,      "ReceiveDataNode",      "\u63A5\u6536\u6570\u636E", "Receive Data",
            NodeBase::OUTPUT, "\u7ED3\u679C\u8F93\u51FA");
    VFP_REG(SendDataNode,         "SendDataNode",         "\u53D1\u9001\u6570\u636E", "Send Data",
            NodeBase::OUTPUT, "\u7ED3\u679C\u8F93\u51FA");
    VFP_REG(ProtocolParseNode,    "ProtocolParseNode",    "\u534F\u8BAE\u89E3\u6790", "Protocol Parse",
            NodeBase::OUTPUT, "\u7ED3\u679C\u8F93\u51FA");
    VFP_REG(FormatNode,           "FormatNode",           "\u683C\u5F0F\u5316", "Format",
            NodeBase::OUTPUT, "\u7ED3\u679C\u8F93\u51FA");

    // ===== 逻辑与数据：Phase5 =====
    VFP_REG(FormulaNode,          "FormulaNode",          "\u516C\u5F0F\u8BA1\u7B97", "Formula",
            NodeBase::LOGIC, "\u903B\u8F91\u4E0E\u63A7\u5236");
    VFP_REG(DelayNode,            "DelayNode",            "\u5EF6\u65F6", "Delay",
            NodeBase::LOGIC, "\u903B\u8F91\u4E0E\u63A7\u5236");
    VFP_REG(CounterNode,          "CounterNode",          "\u6761\u4EF6\u8BA1\u6570", "Counter",
            NodeBase::LOGIC, "\u903B\u8F91\u4E0E\u63A7\u5236");
    VFP_REG(FilterNode,           "FilterNode",           "\u6570\u636E\u7B5B\u9009", "Filter",
            NodeBase::LOGIC, "\u903B\u8F91\u4E0E\u63A7\u5236");
    VFP_REG(ClassifyNode,         "ClassifyNode",         "\u6570\u636E\u5206\u7C7B", "Classify",
            NodeBase::LOGIC, "\u903B\u8F91\u4E0E\u63A7\u5236");
    VFP_REG(SortNode,             "SortNode",             "\u6570\u636E\u6392\u5E8F", "Sort",
            NodeBase::LOGIC, "\u903B\u8F91\u4E0E\u63A7\u5236");
    VFP_REG(RecordNode,           "RecordNode",           "\u6570\u636E\u8BB0\u5F55", "Record",
            NodeBase::OUTPUT, "\u7ED3\u679C\u8F93\u51FA");

    r.setDescription(QStringLiteral("ImageReadNode"),
                     QStringLiteral("HALCON 读图（图容器）"));
    r.setDescription(QStringLiteral("HalconImageSourceNode"),
                     QStringLiteral("HALCON 采集接口"));
    r.setDescription(QStringLiteral("DeepOcrNode"),
                     QStringLiteral("HALCON DeepOCR（本软件唯一 HALCON 算法）"));

    r.addAliases(QStringLiteral("OpencvThresholdNode"),
                 {QStringLiteral("OpenCV二值化"), QStringLiteral("OpenCV Threshold")});
    r.addAliases(QStringLiteral("OpencvAdaptiveThresholdNode"),
                 {QStringLiteral("OpenCV自适应阈值"), QStringLiteral("OpenCV Adaptive Threshold")});
    r.addAliases(QStringLiteral("OpencvCropNode"),
                 {QStringLiteral("OpenCV ROI裁剪"), QStringLiteral("OpenCV ROI Crop")});
    r.addAliases(QStringLiteral("OpencvImageArithNode"),
                 {QStringLiteral("OpenCV图像运算"), QStringLiteral("OpenCV Image Arith")});
    r.addAliases(QStringLiteral("OpencvRotateNode"),
                 {QStringLiteral("OpenCV图像旋转"), QStringLiteral("OpenCV Rotate")});
    r.addAliases(QStringLiteral("OpencvPixelStatsNode"),
                 {QStringLiteral("OpenCV灰度统计"), QStringLiteral("OpenCV Pixel Stats")});
    r.addAliases(QStringLiteral("OpencvBlobNode"),
                 {QStringLiteral("OpenCV Blob分析"), QStringLiteral("OpenCV Blob")});
    r.addAliases(QStringLiteral("OpencvMorphNode"),
                 {QStringLiteral("OpenCV形态学"), QStringLiteral("OpenCV Morphology")});
    r.addAliases(QStringLiteral("OpencvEdgeNode"),
                 {QStringLiteral("OpenCV边缘检测"), QStringLiteral("OpenCV Edge")});
    r.addAliases(QStringLiteral("OpencvFitLineNode"),
                 {QStringLiteral("OpenCV直线拟合"), QStringLiteral("OpenCV Fit Line")});
    r.addAliases(QStringLiteral("OpencvFitCircleNode"),
                 {QStringLiteral("OpenCV圆拟合"), QStringLiteral("OpenCV Fit Circle")});
    r.addAliases(QStringLiteral("OpencvTemplateMatchNode"),
                 {QStringLiteral("OpenCV模板匹配"), QStringLiteral("OpenCV Template Match")});
    r.addAliases(QStringLiteral("OpencvDefectNode"),
                 {QStringLiteral("OpenCV缺陷检测"), QStringLiteral("OpenCV Defect")});
    r.addAliases(QStringLiteral("OpencvCaliperNode"),
                 {QStringLiteral("OpenCV卡尺测量"), QStringLiteral("OpenCV Caliper")});
    r.addAliases(QStringLiteral("OpencvAngleNode"),
                 {QStringLiteral("OpenCV角度测量"), QStringLiteral("OpenCV Angle Measure")});
    r.addAliases(QStringLiteral("DnnDetectNode"),
                 {QStringLiteral("OpenCV目标检测"), QStringLiteral("OpenCV YOLO"), QStringLiteral("ONNX检测")});
    r.addAliases(QStringLiteral("DnnSegmentNode"),
                 {QStringLiteral("OpenCV分割"), QStringLiteral("OpenCV YOLO-Seg"), QStringLiteral("ONNX分割"),
                  QStringLiteral("UNet"), QStringLiteral("语义分割"), QStringLiteral("实例分割")});
    r.addAliases(QStringLiteral("CameraIoNode"),
                 {QStringLiteral("相机IO"), QStringLiteral("相机数字IO"), QStringLiteral("Camera IO"),
                  QStringLiteral("相机触发输出"), QStringLiteral("相机频闪")});
    r.addAliases(QStringLiteral("OpencvTrainClassifierNode"),
                 {QStringLiteral("OpenCV分类器训练"), QStringLiteral("OpenCV Classifier Train")});
    r.addAliases(QStringLiteral("OpencvClassifyNode"),
                 {QStringLiteral("OpenCV分类推理"), QStringLiteral("OpenCV Classify")});
    r.addAliases(QStringLiteral("OpencvCalibNode"),
                 {QStringLiteral("OpenCV相机标定"), QStringLiteral("OpenCV Camera Calibration")});
    r.addAliases(QStringLiteral("OpencvQrNode"),
                 {QStringLiteral("OpenCV二维码解码"), QStringLiteral("OpenCV QR Decode")});
    r.addAliases(QStringLiteral("OpencvTrackNode"),
                 {QStringLiteral("OpenCV目标跟踪"), QStringLiteral("OpenCV Object Track")});
}

#undef VFP_REG

NodeRegistry &NodeRegistry::instance()
{
    static NodeRegistry s_instance;
    return s_instance;
}

NodeRegistry::NodeRegistry()
{
}

void NodeRegistry::registerNode(const NodeRegistration &reg)
{
    // 同名/同 ID 覆盖（允许后续阶段替换实现）
    for (int i = 0; i < m_regs.size(); ++i) {
        if (m_regs[i].id == reg.id) {
            m_regs[i] = reg;
            return;
        }
    }
    m_regs.append(reg);
}

void NodeRegistry::addAliases(const QString &id, const QStringList &aliases)
{
    for (auto &reg : m_regs) {
        if (reg.id == id) {
            reg.aliases = aliases;
            return;
        }
    }
}

void NodeRegistry::setDescription(const QString &id, const QString &description)
{
    for (auto &reg : m_regs) {
        if (reg.id == id) {
            reg.description = description;
            return;
        }
    }
}

NodeBase *NodeRegistry::createById(const QString &id, QObject *parent) const
{
    const NodeRegistration *reg = findById(id);
    if (!reg) return nullptr;
    if (!reg->factory) return nullptr;
    NodeBase *node = reg->factory(parent);
    if (node && node->name().isEmpty())
        node->setName(reg->displayName);
    if (node)
        node->setProperty("vfpNodeTypeId", reg->id); // 复制算子时用于识别具体类型
    return node;
}

NodeBase *NodeRegistry::createByName(const QString &name, QObject *parent) const
{
    const NodeRegistration *reg = findByName(name);
    if (!reg) return nullptr;
    if (!reg->factory) return nullptr;
    NodeBase *node = reg->factory(parent);
    if (node && node->name().isEmpty())
        node->setName(reg->displayName);
    if (node)
        node->setProperty("vfpNodeTypeId", reg->id); // 复制算子时用于识别具体类型
    return node;
}

const NodeRegistration *NodeRegistry::findById(const QString &id) const
{
    for (const auto &reg : m_regs) {
        if (reg.id == id)
            return &reg;
    }
    return nullptr;
}

const NodeRegistration *NodeRegistry::findByName(const QString &name) const
{
    const QString trimmed = name.trimmed();
    for (const auto &reg : m_regs) {
        if (reg.displayName == trimmed
            || reg.englishName.compare(trimmed, Qt::CaseInsensitive) == 0
            || reg.aliases.contains(trimmed, Qt::CaseInsensitive))
            return &reg;
    }
    return nullptr;
}

QStringList NodeRegistry::groups() const
{
    QStringList groups;
    for (const auto &reg : m_regs) {
        if (!groups.contains(reg.group))
            groups.append(reg.group);
    }
    return groups;
}

QList<const NodeRegistration *> NodeRegistry::byGroup(const QString &group) const
{
    QList<const NodeRegistration *> result;
    for (const auto &reg : m_regs) {
        if (reg.group == group)
            result.append(&reg);
    }
    return result;
}
