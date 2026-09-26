cmake_minimum_required(VERSION 3.16)
project(VisionFlowPlatform)

# 启用测试：必须在顶层调用，否则 add_test() 只定义目标而不注册用例，
# 生成的 build/ 目录下不会有 CTestTestfile.cmake，`ctest` 会报 "No tests were found"。
enable_testing()

# 导出 compile_commands.json 用于 clangd 和 MCP 工具
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt 自动编译
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/ui)

# 输出到 bin 目录，方便放 DLL
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui Sql SerialPort Network SerialBus Concurrent Test Charts)

# OpenCV（替换版环境下替代 HALCON 区域链路的节点族；可用 -DOpenCV_DIR=... 覆盖）
# 4.13.0 官方包为 vc16（VS2019）构建，与 VS2022 链接在微软 C++ ABI 兼容保证范围内；
# 其 OpenCVConfig.cmake 对 VS2022 的编译器检查可能仍拒绝，故直接手动指定库。
if(NOT OpenCV_DIR)
    set(OpenCV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/opencv/build")
endif()
set(OpenCV_INCLUDE_DIRS "${OpenCV_DIR}/include")
# Debug/Release 必须配对：MSVC Debug 使用 /MDd，若链接 Release 版 OpenCV（/MD）
# 会造成 CRT 不一致，跨 DLL 传递 STL/堆对象时可能堆损坏。
set(OpenCV_LIBS "$<IF:$<CONFIG:Debug>,${OpenCV_DIR}/x64/vc16/lib/opencv_world4130d.lib,${OpenCV_DIR}/x64/vc16/lib/opencv_world4130.lib>")

# HALCON（可用 CMake -DHALCON_ROOT=... 或环境变量 HALCONROOT 覆盖）
if(NOT DEFINED HALCON_ROOT AND DEFINED ENV{HALCONROOT})
    set(HALCON_ROOT "$ENV{HALCONROOT}")
endif()
if(NOT HALCON_ROOT)
    set(HALCON_ROOT "D:/Program Files/MVTec/HALCON-24.11-Progress-Steady")
endif()
set(HALCON_INCLUDE_DIRS
    ${HALCON_ROOT}/include
    ${HALCON_ROOT}/include/halconcpp
)
set(HALCON_LIBRARIES
    ${HALCON_ROOT}/lib/x64-win64/halconcpp.lib
    ${HALCON_ROOT}/lib/x64-win64/halcon.lib
)

# MVS 海康（可用 CMake -DMVS_ROOT=... 或环境变量 MVSROOT 覆盖）
if(NOT MVS_ROOT AND DEFINED ENV{MVSROOT})
    set(MVS_ROOT "$ENV{MVSROOT}")
endif()
if(NOT MVS_ROOT)
    set(MVS_ROOT "D:/MVS")
endif()
if(NOT EXISTS "${MVS_ROOT}/Development/Includes/MvCameraControl.h")
    message(WARNING
        "未找到 MVS 相机 SDK：${MVS_ROOT}/Development/Includes/MvCameraControl.h\n"
        "  请用 -DMVS_ROOT=<路径> 或设置环境变量 MVSROOT 指向 MVS 安装目录。")
endif()
set(MVS_INCLUDE_DIRS ${MVS_ROOT}/Development/Includes)
set(MVS_LIBRARIES ${MVS_ROOT}/Development/Libraries/win64/MvCameraControl.lib)

# 源文件
set(SOURCES
    src/AppLog.cpp
    src/FlowRuntimeStats.cpp
    src/StatisticsReport.cpp
    src/StatisticsReportDialog.cpp
    src/AppDatabase.cpp
    src/HalconEnvCheck.cpp
    src/HelpDialog.cpp
    src/OpencvUtil.cpp
    src/OpencvThresholdNode.cpp
    src/OpencvAdaptiveThresholdNode.cpp
    src/OpencvAngleNode.cpp
    src/OpencvCropNode.cpp
    src/OpencvImageArithNode.cpp
    src/OpencvRotateNode.cpp
    src/OpencvPixelStatsNode.cpp
    src/OpencvBlobNode.cpp
    src/OpencvMorphNode.cpp
    src/OpencvEdgeNode.cpp
    src/OpencvFitLineNode.cpp
    src/OpencvFitCircleNode.cpp
    src/OpencvTemplateMatchNode.cpp
    src/OpencvFeatureMatchNode.cpp
    src/OpencvDefectNode.cpp
    src/AnomalyBaseline.cpp
    src/AnomalyDetectNode.cpp
    src/OpencvCaliperNode.cpp
    src/OpencvTrainClassifierNode.cpp
    src/OpencvClassifyNode.cpp
    src/DnnInferNode.cpp
    src/DnnDetectNode.cpp
    src/DnnSegmentNode.cpp
    src/CameraIoNode.cpp
    src/DeepOcrNode.cpp
    src/OpencvTrackNode.cpp
    src/OpencvCalibNode.cpp
    src/OpencvQrNode.cpp
    src/ZxingBarcodeNode.cpp
    src/TesseractOcrNode.cpp
    src/VisionWorkbenchStyle.cpp
    src/BrandHeader.cpp
    src/NodeFactory.cpp
    src/NodeRegistry.cpp
    src/MainWindow.cpp
    src/NodeBase.cpp
    src/HalconNode.cpp
    src/NodeSelfTest.cpp
    src/BlurNode.cpp
    src/ThresholdNode.cpp
    src/DilateNode.cpp
    src/ErodeNode.cpp
    src/OpenNode.cpp
    src/CloseNode.cpp
    src/TopHatNode.cpp
    src/BottomHatNode.cpp
    src/RotateImageNode.cpp
    src/TranslateImageNode.cpp
    src/ResizeNode.cpp
    src/MirrorNode.cpp
    src/HistogramEqualizeNode.cpp
    src/ContrastStretchNode.cpp
    src/SharpenNode.cpp
    src/GrayStretchNode.cpp
    src/MedianFilterNode.cpp
    src/ImageAddNode.cpp
    src/ImageSubNode.cpp
    src/ImageMulNode.cpp
    src/ImageDivNode.cpp
    src/ImageInvertNode.cpp
    src/OtsuThresholdNode.cpp
    src/DynThresholdNode.cpp
    src/CropNode.cpp
    src/ConvertImageNode.cpp
    src/PixelStatsNode.cpp
    src/CornerNode.cpp
    src/EdgePointsNode.cpp
    src/CaliperMeasureNode.cpp
    src/FindLineNode.cpp
    src/FindCircleNode.cpp
    src/AngleMeasureNode.cpp
    src/PointLineDistanceNode.cpp
    src/PointCircleDistanceNode.cpp
    src/DistanceMeasureNode.cpp
    src/PositionCorrectNode.cpp
    src/CoordinateTransformNode.cpp
    src/DataCode2DNode.cpp
    src/NPointCalibNode.cpp
    src/CalibrationBoardNode.cpp
    src/HandEyeCalibNode.cpp
    src/CalibrationManager.cpp
    src/BlobAnalysisNode.cpp
    src/EdgeDetectionNode.cpp
    src/AreaNode.cpp
    src/DistanceNode.cpp
    src/ConditionalNode.cpp
    src/LoopNode.cpp
    src/DisplaySinkNode.cpp
    src/WriteFileNode.cpp
    src/TemplateMatchNode.cpp
    src/CalibrationNode.cpp
    src/OcrNode.cpp
    src/BarcodeNode.cpp
    src/FitLineNode.cpp
    src/FitCircleNode.cpp
    src/CommunicationNodeBase.cpp
    src/SerialCommNode.cpp
    src/UdpCommNode.cpp
    src/CommEventEditDialogs.cpp
    src/CommDeviceConfigDialog.cpp
    src/TcpCommNode.cpp
    src/ModbusNode.cpp
    src/ModbusConfigDialog.cpp
    src/PlcCommNode.cpp
    src/PlcConfigDialog.cpp
    src/MvsImageSourceNode.cpp
    src/HalconImageSourceNode.cpp
    src/ColorConversionNode.cpp
    src/DataObject.cpp
    src/FlowScene.cpp
    src/NodeTemplateStore.cpp
    src/FlowExecutor.cpp
    src/HalconWindow.cpp
    src/Connection.cpp
    src/ConnectionGraphicsItem.cpp
    src/ImageReadNode.cpp
    src/NodeGraphicsItem.cpp
    src/Port.cpp
    src/PortGraphicsItem.cpp
    src/PortDataType.cpp
    src/PortConnectivity.cpp
    src/ProjectManager.cpp
    src/AuxPanelManager.cpp
    src/ImageDisplayController.cpp
    src/ExecutionStatusController.cpp
    src/RecentFilesMenu.cpp
    src/GlobalVariableManager.cpp
    src/GlobalVariableDialog.cpp
    src/GlobalCameraManager.cpp
    src/GlobalCameraDialog.cpp
    # Phase 4: Advanced features
    src/UserLoginDialog.cpp
    src/UserManagementDialog.cpp
    src/RecipeManager.cpp
    src/RecipeDialog.cpp
    src/AlarmHistoryDialog.cpp
    src/InspectionResultDialog.cpp
    src/ScriptNode.cpp
    src/ScriptSecurityPolicy.cpp
    # \u901A\u4FE1\u7B97\u5B50
    src/ReceiveDataNode.cpp
    src/SendDataNode.cpp
    # \u901A\u4FE1\u9644\u5C5E\u5DE5\u5177
    src/ProtocolParseNode.cpp
    src/FormatNode.cpp
    # 逻辑与数据：Phase5
    src/FormulaNode.cpp
    src/DelayNode.cpp
    src/SubFlowNode.cpp
    src/CounterNode.cpp
    src/FilterNode.cpp
    src/ClassifyNode.cpp
    src/SortNode.cpp
    src/RecordNode.cpp
    # Communication architecture refactor
    src/CommunicationManager.cpp
    src/CommunicationManagerDialog.cpp
    src/CommMonitorDialog.cpp
    src/ReceiveEvent.cpp
    src/SendEvent.cpp
    src/HeartbeatManager.cpp
    src/GlobalTriggerManager.cpp
    src/GlobalTriggerDialog.cpp
    # Phase 6: 系统级
    src/SessionManager.cpp
    src/FactoryPasswordGuard.cpp
    src/OperationLogDialog.cpp
    src/ParameterSearchDialog.cpp
    src/CodeExportDialog.cpp
    src/ReportDialog.cpp
    # Phase 7: 深度学习
    # 自定义运行界面（对齐 VM4.4）
    src/RuntimeInterface.cpp
    src/RuntimeInterfaceView.cpp
    src/RuntimeInterfaceDesigner.cpp
    # VisionMaster 对标优化组件
    src/PerformancePanel.cpp
    src/OutputDataViewer.cpp
    src/ResultTablePanel.cpp
    src/VariablePanel.cpp
    src/PanelVisibilityStore.cpp
    src/RecoveryStore.cpp
    src/ReportAutoExport.cpp
    src/NodeGroupItem.cpp
    src/FlowSnippet.cpp
    src/YieldMonitor.cpp
    src/NodeSearchWidget.cpp
    src/ConnectionDragHelper.cpp
    src/HelpViewer.cpp
    src/ModuleEditorDialog.cpp
    src/CommentGraphicsItem.cpp
    src/FlowVariableDialog.cpp
    src/AnnotationDialog.cpp
    # 方案加密 / 只读导出（G-P1-10）
    src/SchemeCrypto.cpp
    src/SchemePackage.cpp
    # 国际化（G-P1-5）
    src/I18n.cpp
)

set(HEADERS
    include/AppLog.h
    include/FlowRuntimeStats.h
    include/StatisticsReport.h
    include/StatisticsReportDialog.h
    include/AppDatabase.h
    include/InspectionRecord.h
    include/HalconEnvCheck.h
    include/HelpDialog.h
    include/OpencvUtil.h
    include/OpencvThresholdNode.h
    include/OpencvAdaptiveThresholdNode.h
    include/OpencvAngleNode.h
    include/OpencvCropNode.h
    include/OpencvImageArithNode.h
    include/OpencvRotateNode.h
    include/OpencvPixelStatsNode.h
    include/OpencvBlobNode.h
    include/OpencvMorphNode.h
    include/OpencvEdgeNode.h
    include/OpencvFitLineNode.h
    include/OpencvFitCircleNode.h
    include/OpencvTemplateMatchNode.h
    include/OpencvFeatureMatchNode.h
    include/OpencvDefectNode.h
    include/AnomalyBaseline.h
    include/AnomalyDetectNode.h
    include/OpencvCaliperNode.h
    include/ClassifierTrainer.h
    include/OpencvTrainClassifierNode.h
    include/OpencvClassifyNode.h
    include/DeepOcrNode.h
    include/OpenCvTracker.h
    include/OpencvTrackNode.h
    include/DnnInferNode.h
    include/DnnDetectNode.h
    include/DnnSegmentNode.h
    include/CameraIoNode.h
    include/OpencvCalibNode.h
    include/OpencvQrNode.h
    include/ZxingBarcodeNode.h
    include/TesseractOcrNode.h
    include/VisionWorkbenchStyle.h
    include/BrandHeader.h
    include/VisionIntegrationPolicy.h
    include/ParamSpec.h
    include/NodeFactory.h
    include/NodeRegistry.h
    include/MainWindow.h
    include/NodeBase.h
    include/HalconNode.h
    include/NodeSelfTest.h
    include/BlurNode.h
    include/ThresholdNode.h
    include/DilateNode.h
    include/ErodeNode.h
    include/OpenNode.h
    include/CloseNode.h
    include/TopHatNode.h
    include/BottomHatNode.h
    include/RotateImageNode.h
    include/TranslateImageNode.h
    include/ResizeNode.h
    include/MirrorNode.h
    include/HistogramEqualizeNode.h
    include/ContrastStretchNode.h
    include/SharpenNode.h
    include/GrayStretchNode.h
    include/MedianFilterNode.h
    include/ImageAddNode.h
    include/ImageSubNode.h
    include/ImageMulNode.h
    include/ImageDivNode.h
    include/ImageInvertNode.h
    include/OtsuThresholdNode.h
    include/DynThresholdNode.h
    include/CropNode.h
    include/ConvertImageNode.h
    include/PixelStatsNode.h
    include/CornerNode.h
    include/EdgePointsNode.h
    include/CaliperMeasureNode.h
    include/FindLineNode.h
    include/FindCircleNode.h
    include/AngleMeasureNode.h
    include/PointLineDistanceNode.h
    include/PointCircleDistanceNode.h
    include/DistanceMeasureNode.h
    include/PositionCorrectNode.h
    include/CoordinateTransformNode.h
    include/DataCode2DNode.h
    include/NPointCalibNode.h
    include/CalibrationBoardNode.h
    include/HandEyeCalibNode.h
    include/CalibrationManager.h
    include/BlobAnalysisNode.h
    include/EdgeDetectionNode.h
    include/AreaNode.h
    include/DistanceNode.h
    include/ConditionalNode.h
    include/LoopNode.h
    include/DisplaySinkNode.h
    include/WriteFileNode.h
    include/TemplateMatchNode.h
    include/CalibrationNode.h
    include/OcrNode.h
    include/BarcodeNode.h
    include/FitLineNode.h
    include/FitCircleNode.h
    include/CommunicationNodeBase.h
    include/SerialCommNode.h
    include/UdpCommNode.h
    include/CommEventEditDialogs.h
    include/CommDeviceConfigDialog.h
    include/TcpCommNode.h
    include/ModbusNode.h
    include/ModbusConfigDialog.h
    include/PlcCommNode.h
    include/PlcConfigDialog.h
    include/registerbyteorder.h
    # Phase 4: Advanced features
    include/UserLoginDialog.h
    include/UserManagementDialog.h
    include/RecipeManager.h
    include/RecipeDialog.h
    include/AlarmHistoryDialog.h
    include/InspectionResultDialog.h
    include/ScriptNode.h
    include/ScriptSecurityPolicy.h
    # \u901A\u4FE1\u7B97\u5B50
    include/ReceiveDataNode.h
    include/SendDataNode.h
    # \u901A\u4FE1\u9644\u5C5E\u5DE5\u5177
    include/ProtocolParseNode.h
    include/FormatNode.h
    # 逻辑与数据：Phase5
    include/FormulaNode.h
    include/DelayNode.h
    include/SubFlowNode.h
    include/SubFlowDefs.h
    include/CounterNode.h
    include/FilterNode.h
    include/ClassifyNode.h
    include/SortNode.h
    include/RecordNode.h
    include/MvsImageSourceNode.h
    include/HalconImageSourceNode.h
    include/ColorConversionNode.h
    include/DataObject.h
    include/FlowScene.h
    include/NodeTemplateStore.h
    include/FlowExecutor.h
    include/HalconWindow.h
    include/Connection.h
    include/ConnectionGraphicsItem.h
    include/ImageReadNode.h
    include/NodeGraphicsItem.h
    include/Port.h
    include/PortGraphicsItem.h
    include/PortDataType.h
    include/PortConnectivity.h
    include/ProjectManager.h
    include/AuxPanelManager.h
    include/ImageDisplayController.h
    include/ExecutionStatusController.h
    include/RecentFilesMenu.h
    include/RecoveryStore.h
    include/ReportAutoExport.h
    include/NodeGroupItem.h
    include/FlowSnippet.h
    include/YieldMonitor.h
    include/GlobalVariableManager.h
    include/GlobalVariableDialog.h
    include/GlobalCameraManager.h
    include/GlobalCameraDialog.h
    # Communication architecture refactor
    include/CommunicationManager.h
    include/CommunicationManagerDialog.h
    include/CommMonitorDialog.h
    include/ReceiveEvent.h
    include/SendEvent.h
    include/HeartbeatManager.h
    include/GlobalTriggerManager.h
    include/GlobalTriggerDialog.h
    # Phase 6: 系统级
    include/SessionManager.h
    include/OperationLogDialog.h
    include/ParameterSearchDialog.h
    include/CodeExportDialog.h
    include/ReportDialog.h
    # Phase 7: 深度学习
    # 自定义运行界面（对齐 VM4.4）
    include/RuntimeInterface.h
    include/RuntimeInterfaceView.h
    include/RuntimeInterfaceDesigner.h
    # VisionMaster 对标优化组件
    include/PerformancePanel.h
    include/OutputDataViewer.h
    include/NodeSearchWidget.h
    include/ConnectionDragHelper.h
    include/HelpViewer.h
    include/ResultTablePanel.h
    include/VariablePanel.h
    include/ModuleEditorDialog.h
    include/CommentGraphicsItem.h
    include/FlowVariableDialog.h
    include/AnnotationDialog.h
    # 方案加密 / 只读导出（G-P1-10）
    include/SchemeCrypto.h
    include/SchemePackage.h
    include/I18n.h
)

set(UI_FILES
    ui/MainWindow.ui
)

# ZXing-C++（条码解码，Apache 2.0；thirdparty/zxing 由源码编译安装）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/zxing/lib/cmake/ZXing/ZXingConfig.cmake")
    set(ZXing_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/zxing/lib/cmake/ZXing")
    find_package(ZXing REQUIRED)
endif()

# Tesseract OCR（Apache 2.0；thirdparty/tesseract 由源码编译安装）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tesseract/lib/cmake/tesseract/TesseractConfig.cmake")
    set(Leptonica_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/leptonica/lib/cmake/leptonica")
    set(tesseract_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tesseract/lib/cmake/tesseract")
    find_package(tesseract REQUIRED)
endif()

# ---------------------------------------------------------------
# 核心对象库
# ---------------------------------------------------------------
# 除 main.cpp 外的全部源码放入 OBJECT 库，只编译一次，主程序与各测试目标
# 通过 target_link_libraries(... vfp_core) 直接复用其目标文件与使用要求。
#
# 之所以必须这样做：节点基类 HalconNode 的参数面板会访问 FlowScene /
# GlobalVariableManager，而 FlowScene 又依赖 NodeFactory → NodeRegistry →
# 全部 ~64 个算子。任何测试只要链接到节点实现，就必然需要整个工程的目标文件。
# 原先各测试目标只列出少量 .cpp，链接期必然失败；同时还会重复编译上百个文件。
add_library(vfp_core OBJECT
    ${SOURCES}
    ${HEADERS}
    ${UI_FILES}
)

# 嵌入手册（软件内 F1 查看的兜底资源；外部 docs/ 文件优先覆盖）
# 放在 OBJECT 库上：生成的目标文件会被所有使用方包含，资源注册不会丢失
qt_add_resources(vfp_core manual_resources
    PREFIX "/"
    FILES "docs/用户操作手册.md"
)

# PUBLIC：使用方（主程序/测试）自动继承头文件路径与链接库
target_include_directories(vfp_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${HALCON_INCLUDE_DIRS}
    ${MVS_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/zxing/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tesseract/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/leptonica/include"
)

target_link_libraries(vfp_core PUBLIC
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    Qt6::Charts
    Qt6::Sql
    Qt6::SerialPort
    Qt6::Network
    Qt6::SerialBus
    Qt6::Concurrent
    ${HALCON_LIBRARIES}
    ${MVS_LIBRARIES}
    ${OpenCV_LIBS}
    dbghelp.lib
    psapi.lib
)
if(TARGET ZXing::ZXing)
    target_link_libraries(vfp_core PUBLIC ZXing::ZXing)
endif()
if(TARGET Tesseract::libtesseract)
    target_link_libraries(vfp_core PUBLIC Tesseract::libtesseract)
endif()

# Windows 平台：彻底解决 UTF-8 警告与 PDB 并发写冲突
if(WIN32 AND MSVC)
    target_compile_options(vfp_core PUBLIC
        /utf-8           # 保证项目中文不乱码（使用方同样需要）
    )
    target_compile_options(vfp_core PRIVATE
        /wd4828          # 直接屏蔽 MVS 头文件编码警告（最有效）
        /FS              # 强制同步 PDB 写入，解决 C1041 多进程 PDB 锁冲突
    )
endif()

# ---------------------------------------------------------------
# 主程序：仅 main.cpp + 核心对象库
# ---------------------------------------------------------------
add_executable(VisionFlowPlatform src/main.cpp)
target_link_libraries(VisionFlowPlatform PRIVATE vfp_core)

if(WIN32)
    set_target_properties(VisionFlowPlatform PROPERTIES
        WIN32_EXECUTABLE ON
    )
endif()

# ---------------------------------------------------------------
# 国际化（G-P1-5）：把仓库内的 .ts 编译为 .qm 并部署到运行/分发目录。
# 更新翻译源：源码改动后执行 `lupdate src ui -ts translations/visionflow_en.ts`，
# 再用 translations/apply_en_translations.py 填入英文，最后重跑本段生成 .qm。
# 直接用 lrelease 可执行文件（避免依赖可能未随 Qt6 主包发布的 LinguistTools CMake 组件）。
# ---------------------------------------------------------------
set(VFP_TS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/translations")
set(VFP_TS "${VFP_TS_DIR}/visionflow_en.ts")
set(VFP_QM "${VFP_TS_DIR}/visionflow_en.qm")
# 从 Qt6Core 的安装前缀推导 bin 目录（<prefix>/lib/cmake/Qt6Core -> <prefix>/bin）
get_filename_component(_VFP_QT_PREFIX "${Qt6Core_DIR}/../../.." ABSOLUTE)
find_program(VFP_LRELEASE_EXECUTABLE
    NAMES lrelease lrelease-qt6
    HINTS "${_VFP_QT_PREFIX}/bin" "${Qt6_DIR}/../../../bin")
if(VFP_LRELEASE_EXECUTABLE AND EXISTS "${VFP_TS}")
    add_custom_command(
        OUTPUT "${VFP_QM}"
        COMMAND "${VFP_LRELEASE_EXECUTABLE}" "${VFP_TS}" -qm "${VFP_QM}"
        DEPENDS "${VFP_TS}"
        COMMENT "编译英文翻译 visionflow_en.qm")
    add_custom_target(vfp_translations DEPENDS "${VFP_QM}")
    add_dependencies(VisionFlowPlatform vfp_translations)

    # 部署到构建输出目录（build/bin/<config>/translations），供本地运行与测试加载
    add_custom_command(TARGET VisionFlowPlatform POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:VisionFlowPlatform>/translations"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VFP_QM}"
                "$<TARGET_FILE_DIR:VisionFlowPlatform>/translations/")
    # 同步部署到分发目录（dist/VisionFlowPlatform/translations），供现场运行加载
    add_custom_command(TARGET VisionFlowPlatform POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/dist/VisionFlowPlatform/translations"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VFP_QM}"
                "${CMAKE_CURRENT_SOURCE_DIR}/dist/VisionFlowPlatform/translations/")
endif()

# 可选：HALCON 冒烟测试（验证测量链路算子参数与官方签名一致）
option(VFP_BUILD_TESTS "Build HALCON smoke tests" OFF)
if(VFP_BUILD_TESTS)
    add_executable(halcon_measure_smoke tests/halcon_measure_smoke.cpp)
    target_include_directories(halcon_measure_smoke PRIVATE ${HALCON_INCLUDE_DIRS})
    target_link_libraries(halcon_measure_smoke PRIVATE ${HALCON_LIBRARIES})

    # HALCON 节点算子族可用性验证（决定节点保留/移除）
    # 注意：这两个目标编译 src/OpencvUtil.cpp，而 OpencvUtil.h 依赖 QImage，
    # 必须链接 Qt6::Gui 以带入 Qt 头文件路径（原先缺失 → error C1083: 无法打开 "QImage"）
    add_executable(halcon_node_probe tests/halcon_node_probe.cpp src/OpencvUtil.cpp)
    target_include_directories(halcon_node_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${HALCON_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(halcon_node_probe PRIVATE ${HALCON_LIBRARIES} ${OpenCV_LIBS} Qt6::Gui)

    # 保留 HALCON 节点数值级验证（与 OpenCV 逐像素对比 + 多序列稳定性）
    add_executable(halcon_node_verify tests/halcon_node_verify.cpp src/OpencvUtil.cpp)
    target_include_directories(halcon_node_verify PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${HALCON_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(halcon_node_verify PRIVATE ${HALCON_LIBRARIES} ${OpenCV_LIBS} Qt6::Gui)
    if(MSVC)
        target_compile_options(halcon_node_verify PRIVATE /utf-8)
    endif()
    if(MSVC)
        # 测试源文件含中文注释，统一按 UTF-8 解析（与主 target 一致）
        target_compile_options(halcon_node_probe PRIVATE /utf-8)
        target_compile_options(halcon_measure_smoke PRIVATE /utf-8)
    endif()
    if(MSVC)
        target_compile_options(halcon_measure_smoke PRIVATE /utf-8)
    endif()
    # HALCON 功能诊断（license/区域/绘制/测量/模板等算子族可用性矩阵）
    add_executable(halcon_function_probe tests/halcon_function_probe.cpp)
    target_include_directories(halcon_function_probe PRIVATE ${HALCON_INCLUDE_DIRS})
    target_link_libraries(halcon_function_probe PRIVATE ${HALCON_LIBRARIES})
    if(MSVC)
        target_compile_options(halcon_function_probe PRIVATE /utf-8)
    endif()
    # 替换版缺陷模式定位（顺序敏感性/尺寸矩阵）
    add_executable(halcon_defect_probe tests/halcon_defect_probe.cpp)
    target_include_directories(halcon_defect_probe PRIVATE ${HALCON_INCLUDE_DIRS})
    target_link_libraries(halcon_defect_probe PRIVATE ${HALCON_LIBRARIES})
    if(MSVC)
        target_compile_options(halcon_defect_probe PRIVATE /utf-8)
    endif()
    # 环境自检模块验证
    add_executable(halcon_envcheck_demo tests/halcon_envcheck_demo.cpp src/HalconEnvCheck.cpp)
    target_include_directories(halcon_envcheck_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${HALCON_INCLUDE_DIRS})
    target_link_libraries(halcon_envcheck_demo PRIVATE ${HALCON_LIBRARIES} Qt6::Core)
    if(MSVC)
        target_compile_options(halcon_envcheck_demo PRIVATE /utf-8)
    endif()
    # 像素访问验证（OpenCV 桥接前提）
    add_executable(halcon_pixel_probe tests/halcon_pixel_probe.cpp)
    target_include_directories(halcon_pixel_probe PRIVATE ${HALCON_INCLUDE_DIRS})
    target_link_libraries(halcon_pixel_probe PRIVATE ${HALCON_LIBRARIES})
    if(MSVC)
        target_compile_options(halcon_pixel_probe PRIVATE /utf-8)
    endif()
    # OpenCV 桥接与节点算法验证
    add_executable(opencv_nodes_test tests/opencv_nodes_test.cpp src/OpencvUtil.cpp)
    target_include_directories(opencv_nodes_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${HALCON_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS})
    # 同 halcon_node_probe：OpencvUtil.h 依赖 QImage，需要 Qt6::Gui 的头文件路径
    target_link_libraries(opencv_nodes_test PRIVATE ${HALCON_LIBRARIES} ${OpenCV_LIBS} Qt6::Gui)
    if(TARGET ZXing::ZXing)
        target_link_libraries(opencv_nodes_test PRIVATE ZXing::ZXing)
    endif()
    if(TARGET Tesseract::libtesseract)
        target_link_libraries(opencv_nodes_test PRIVATE Tesseract::libtesseract)
        target_include_directories(opencv_nodes_test PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tesseract/include")
    endif()
    if(MSVC)
        target_compile_options(opencv_nodes_test PRIVATE /utf-8)
    endif()
    # 独立最小实验：棋盘检测
    add_executable(calib_min_probe tests/calib_min_probe.cpp)
    target_include_directories(calib_min_probe PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(calib_min_probe PRIVATE ${OpenCV_LIBS})
    if(MSVC)
        target_compile_options(calib_min_probe PRIVATE /utf-8)
    endif()
    # OpenCV ML 分类器 训练->推理 闭环测试
    add_executable(classifier_train_test tests/classifier_train_test.cpp)
    target_include_directories(classifier_train_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(classifier_train_test PRIVATE ${OpenCV_LIBS})
    if(MSVC)
        target_compile_options(classifier_train_test PRIVATE /utf-8)
    endif()
    # ONNX 深度学习推理链路测试（模型由 tests/gen_min_onnx.py 生成）
    add_executable(dnn_infer_test tests/dnn_infer_test.cpp)
    target_include_directories(dnn_infer_test PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(dnn_infer_test PRIVATE ${OpenCV_LIBS})
    if(MSVC)
        target_compile_options(dnn_infer_test PRIVATE /utf-8)
    endif()
    # 官方预训练 .hdl 模型端到端推理验证（Dl* 推理节点同款链路）
    add_executable(dl_cls_probe tests/dl_cls_probe.cpp)
    target_include_directories(dl_cls_probe PRIVATE ${HALCON_INCLUDE_DIRS})
    target_link_libraries(dl_cls_probe PRIVATE ${HALCON_LIBRARIES})
    if(MSVC)
        target_compile_options(dl_cls_probe PRIVATE /utf-8)
    endif()
    # HALCON DL 与 OpenCV DNN 可用性诊断
    add_executable(halcon_dl_probe tests/halcon_dl_probe.cpp)
    target_include_directories(halcon_dl_probe PRIVATE ${HALCON_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(halcon_dl_probe PRIVATE ${HALCON_LIBRARIES} ${OpenCV_LIBS})
    if(MSVC)
        target_compile_options(halcon_dl_probe PRIVATE /utf-8)
    endif()
    # OCR/条码/测量 替换版可用性诊断
    add_executable(halcon_ocr_barcode_probe tests/halcon_ocr_barcode_probe.cpp)
    target_include_directories(halcon_ocr_barcode_probe PRIVATE ${HALCON_INCLUDE_DIRS})
    target_link_libraries(halcon_ocr_barcode_probe PRIVATE ${HALCON_LIBRARIES})
    if(MSVC)
        target_compile_options(halcon_ocr_barcode_probe PRIVATE /utf-8)
    endif()
    # DL OCR（DeepOcr）替换版可用性
    add_executable(halcon_deepocr_probe tests/halcon_deepocr_probe.cpp)
    target_include_directories(halcon_deepocr_probe PRIVATE ${HALCON_INCLUDE_DIRS})
    target_link_libraries(halcon_deepocr_probe PRIVATE ${HALCON_LIBRARIES})
    if(MSVC)
        target_compile_options(halcon_deepocr_probe PRIVATE /utf-8)
    endif()
    # XLD 轮廓 + DL 训练/推理 附加能力探针（run_env_check.bat [4/4]）
    add_executable(halcon_extra_probe tests/halcon_extra_probe.cpp)
    target_include_directories(halcon_extra_probe PRIVATE ${HALCON_INCLUDE_DIRS})
    target_link_libraries(halcon_extra_probe PRIVATE ${HALCON_LIBRARIES})
    if(MSVC)
        target_compile_options(halcon_extra_probe PRIVATE /utf-8)
    endif()
endif()
if(MSVC)
endif()

if(MSVC)
endif()

if(MSVC)
    add_executable(hde_train_probe tests/hde_train_probe.cpp)
    target_include_directories(hde_train_probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests ${HALCON_INCLUDE_DIRS}
        ${HALCON_ROOT}/include/hdevengine)
    target_link_libraries(hde_train_probe PRIVATE
        ${HALCON_ROOT}/lib/x64-win64/hdevenginecpp.lib ${HALCON_LIBRARIES})
    target_compile_options(hde_train_probe PRIVATE /utf-8)
endif()

if(MSVC)
    add_executable(tracker_test tests/tracker_test.cpp)
    target_include_directories(tracker_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(tracker_test PRIVATE ${OpenCV_LIBS})
    target_compile_options(tracker_test PRIVATE /utf-8)
endif()

# QtTest 用例统一经 tools/run_qtest.cmake 转发结果（W-A）：QtTest 的 plain-text 日志通道在
# stdout 被重定向时（ctest 的管道即是）一条都不写，故直接 add_test(COMMAND exe) 收不到任何
# PASS/FAIL 明细，门禁变红却无从定位。包装用 cmake.exe 的 stdout 把结果文件内容打给 ctest，
# 并加"无 Totals / 有 failed / 0 passed 即判红"三道闸。根因与实测对照见该文件头。
function(vfp_add_qtest)
    cmake_parse_arguments(QTEST "" "NAME;TARGET" "ARGS" ${ARGN})
    if(NOT QTEST_NAME OR NOT QTEST_TARGET)
        message(FATAL_ERROR "vfp_add_qtest 需要 NAME 与 TARGET")
    endif()
    # ARGS 作为分号列表整体传给 -DVFP_ARGS，由 run_qtest.cmake 按列表展开。实测两种写法
    # （单个 -DVFP_ARGS=a;b;c 与逐条 -DVFP_ARGn）都能完整到达子进程，含空格的单个 argv
    # 也保得住；真正会出错的是脚本里按空格 separate_arguments，那会把 "a b" 拆成两个。
    add_test(NAME ${QTEST_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DVFP_EXE=$<TARGET_FILE:${QTEST_TARGET}>"
                "-DVFP_LOG=${CMAKE_CURRENT_BINARY_DIR}/Testing/${QTEST_NAME}.txt"
                "-DVFP_ARGS=${QTEST_ARGS}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qtest.cmake")
endfunction()

# 节点核心功能单元测试（参数管理、序列化、端口、公式求值）
# 链接 vfp_core 即可获得全部算子实现、AUTOMOC 生成物与依赖库
if(MSVC)
    add_executable(node_core_test tests/node_core_test.cpp)
    target_link_libraries(node_core_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME NodeCoreTest TARGET node_core_test)
endif()

# OpenCV 节点序列化测试（toJson/fromJson 完整性验证）
if(MSVC)
    add_executable(opencv_serialization_test tests/opencv_serialization_test.cpp)
    target_link_libraries(opencv_serialization_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME OpencvSerializationTest TARGET opencv_serialization_test)
endif()

# OpenCV 角度/面积测量功能测试（🥇：角度几何计算 + Blob 面积端口）
if(MSVC)
    add_executable(opencv_angle_area_test tests/opencv_angle_area_test.cpp)
    target_link_libraries(opencv_angle_area_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME OpencvAngleAreaTest TARGET opencv_angle_area_test)
endif()

# ONNX 目标检测功能测试（🥈：YOLO 输出解析/NMS/坐标映射 + 序列化 + 缺模型降级）
if(MSVC)
    add_executable(dnn_detect_test tests/dnn_detect_test.cpp)
    target_link_libraries(dnn_detect_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME DnnDetectTest TARGET dnn_detect_test)
endif()

# ONNX 分割功能测试（🥇 G-P0-2：语义/实例输出解析 + 掩膜几何 + 连通域 + 降级 + 端到端）
if(MSVC)
    add_executable(dnn_segment_test tests/dnn_segment_test.cpp)
    target_link_libraries(dnn_segment_test PRIVATE vfp_core Qt6::Test)
    # 端到端用例用 tests/gen_min_seg_onnx.py 现生成最小模型（需要 python+onnx；
    # 不可用时该用例自动 QSKIP，不把它变成环境相关的红灯）
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" VFP_TEST_SRC_DIR)
    target_compile_definitions(dnn_segment_test PRIVATE
        VFP_TEST_SOURCE_DIR="${VFP_TEST_SRC_DIR}")
    vfp_add_qtest(NAME DnnSegmentTest TARGET dnn_segment_test)
endif()

# OpenCV 缺陷检测（标准件/黄金模板比对）功能测试（P1-1：基础差影 + 对齐归一化 + 边缘抑制）
if(MSVC)
    add_executable(opencv_defect_align_test tests/opencv_defect_align_test.cpp)
    target_link_libraries(opencv_defect_align_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME OpencvDefectAlignTest TARGET opencv_defect_align_test)
endif()

# 特征匹配（阶段 C 的 C1）：合成图上的旋转/尺度定位（角度/尺度/中心有真值）+ 三条失败面
# + FLANN 不适用时的改路留痕 + 冒烟合成件"无特征可教学"归类；序列化用例在 OpencvSerializationTest
if(MSVC)
    add_executable(feature_match_test tests/feature_match_test.cpp)
    target_link_libraries(feature_match_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME FeatureMatchTest TARGET feature_match_test)
endif()

# 标定链路（阶段 C 的 C2）：已知内参 K 投影的合成帧驱动 OpencvCalibNode（①②）+ N 点/手眼矩阵对真值
# （④⑤）+ CalibrationManager 与坐标换算消费端（⑥）+ 失败路径判红回归锁（⑦，§3.2 结论 D）
if(MSVC)
    add_executable(calib_chain_test tests/calib_chain_test.cpp)
    target_link_libraries(calib_chain_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME CalibChainTest TARGET calib_chain_test)
endif()

# 异常检测（G-P1-2 后端 A：OK 样本逐像素统计基线）功能测试
if(MSVC)
    add_executable(anomaly_detect_test tests/anomaly_detect_test.cpp)
    target_link_libraries(anomaly_detect_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME AnomalyDetectTest TARGET anomaly_detect_test)
endif()

# 全节点 execute() 冒烟门禁（P0-1 回归锁）：遍历注册表，默认参数 + 合成图必须成功
if(MSVC)
    add_executable(node_execute_smoke_test tests/node_execute_smoke_test.cpp)
    target_link_libraries(node_execute_smoke_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME NodeExecuteSmokeTest TARGET node_execute_smoke_test)
endif()

# 统计报表（P1-11）纯计算与导出测试
if(MSVC)
    add_executable(statistics_report_test tests/statistics_report_test.cpp)
    target_link_libraries(statistics_report_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME StatisticsReportTest TARGET statistics_report_test)
endif()

# 长稳（soak）测试：连续模式长时间跑真实链路，看轮次/失败/句柄/内存的增长趋势（NFR2.4）
# 时长由 VFP_SOAK_SECONDS 控制（CI 用 20 秒；现场 72h 长跑用 tools/soak.ps1）
if(MSVC)
    add_executable(soak_test tests/soak_test.cpp)
    target_link_libraries(soak_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME SoakTest TARGET soak_test)
    set_tests_properties(SoakTest PROPERTIES ENVIRONMENT "VFP_SOAK_SECONDS=20")
endif()

# 相机 IO 控制功能测试（🥉：MVS 数字 IO 逻辑 + 序列化 + 缺相机降级）
if(MSVC)
    add_executable(camera_io_test tests/camera_io_test.cpp)
    target_link_libraries(camera_io_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME CameraIoTest TARGET camera_io_test)
endif()

# 集成测试 - 测试多算子组合的流程执行
if(MSVC)
    add_executable(integration_test tests/integration_test.cpp)
    target_link_libraries(integration_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME IntegrationTest TARGET integration_test)
endif()

# GUI 用例公共环境：注入 Qt 平台插件与运行时 DLL 目录，不依赖调用方 shell 的 PATH；
# 否则（尤其自托管 runner / 干净环境）进程会在启动阶段就以 0xc0000135（找不到 DLL）失败。
function(vfp_configure_gui_test _vfp_test_name)
    if(CMAKE_VERSION VERSION_LESS 3.22)
        message(WARNING "${_vfp_test_name}: CMake < 3.22，跳过测试环境注入（可能因缺少 Qt 平台插件而失败）")
        return()
    endif()
    set(_vfp_mods
        "QT_PLUGIN_PATH=path_list_prepend:${CMAKE_CURRENT_SOURCE_DIR}/dist/VisionFlowPlatform"
        "PATH=path_list_prepend:${CMAKE_CURRENT_SOURCE_DIR}/dist/VisionFlowPlatform")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/opencv/build/x64/vc16/bin")
        list(APPEND _vfp_mods
            "PATH=path_list_prepend:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/opencv/build/x64/vc16/bin")
    endif()
    set_tests_properties(${_vfp_test_name} PROPERTIES ENVIRONMENT_MODIFICATION "${_vfp_mods}")
endfunction()

# 模块编辑窗「自动重算」挂钩：参数控件变更 → 去抖 → 请求重算下游
# 需要 Qt6::Widgets（QTEST_MAIN 据此创建 QApplication），且不 show() 窗口，可在 CI 稳定运行
if(MSVC)
    add_executable(module_editor_test tests/module_editor_test.cpp)
    target_link_libraries(module_editor_test PRIVATE vfp_core Qt6::Test Qt6::Widgets)
    vfp_add_qtest(NAME ModuleEditorTest TARGET module_editor_test)
    vfp_configure_gui_test(ModuleEditorTest)
endif()

# 结果数据表：同一模块原地更新 / 失败可见 / CSV 导出
if(MSVC)
    add_executable(result_table_test tests/result_table_test.cpp)
    target_link_libraries(result_table_test PRIVATE vfp_core Qt6::Test Qt6::Widgets)
    vfp_add_qtest(NAME ResultTableTest TARGET result_table_test)
    vfp_configure_gui_test(ResultTableTest)
endif()

# 变量面板：引用表达式语法 / 同一模块原地更新 / 全局变量可见 / 插入到编辑控件
if(MSVC)
    add_executable(variable_panel_test tests/variable_panel_test.cpp)
    target_link_libraries(variable_panel_test PRIVATE vfp_core Qt6::Test Qt6::Widgets)
    vfp_add_qtest(NAME VariablePanelTest TARGET variable_panel_test)
    vfp_configure_gui_test(VariablePanelTest)
endif()

# 辅助面板显隐持久化：键定义 + 跨重启往返（用例本身不需要 GUI，但同样链接 vfp_core，
# 因此仍需注入运行时 DLL 目录：干净环境下否则会在启动阶段以 0xc0000135 失败）
if(MSVC)
    add_executable(panel_visibility_test tests/panel_visibility_test.cpp)
    target_link_libraries(panel_visibility_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME PanelVisibilityTest TARGET panel_visibility_test)
    vfp_configure_gui_test(PanelVisibilityTest)
endif()

# 崩溃恢复 / 自动保存：往返一致 / 真加载器可打开 / 坏文件拒收 / 未保存判定 / 干净退出清理
if(MSVC)
    add_executable(recovery_store_test tests/recovery_store_test.cpp)
    target_link_libraries(recovery_store_test PRIVATE vfp_core Qt6::Test Qt6::Widgets)
    vfp_add_qtest(NAME RecoveryStoreTest TARGET recovery_store_test)
    vfp_configure_gui_test(RecoveryStoreTest)
endif()

# 算子分组（FR1.9）：成员身份跨存盘/撤销保持 / 分组不改变执行 / 帧体跟随 / 编辑锁定
if(MSVC)
    add_executable(node_group_test tests/node_group_test.cpp)
    target_link_libraries(node_group_test PRIVATE vfp_core Qt6::Test Qt6::Widgets)
    vfp_add_qtest(NAME NodeGroupTest TARGET node_group_test)
    vfp_configure_gui_test(NodeGroupTest)
endif()

# 子图片段（FR15.10 设计期复用）：拓扑往返 / 重新发号与去重 / 原子性 / 一步撤销 / 粘完能跑
if(MSVC)
    add_executable(flow_snippet_test tests/flow_snippet_test.cpp)
    target_link_libraries(flow_snippet_test PRIVATE vfp_core Qt6::Test Qt6::Widgets)
    vfp_add_qtest(NAME FlowSnippetTest TARGET flow_snippet_test)
    vfp_configure_gui_test(FlowSnippetTest)
endif()

# 运行期子流程（FR15.10）：定义校验 / extras 往返 / 内联执行次数 / 双调用点隔离 / 递归与未定义拒绝
if(MSVC)
    add_executable(subflow_test tests/subflow_test.cpp)
    target_link_libraries(subflow_test PRIVATE vfp_core Qt6::Test Qt6::Widgets)
    vfp_add_qtest(NAME SubFlowTest TARGET subflow_test)
    vfp_configure_gui_test(SubFlowTest)
endif()

# 良率目标监控（P1-11）：闭锁只报一次 / 恢复重置 / 滚动窗口 / 最小样本 / 配置变更重置
if(MSVC)
    add_executable(yield_monitor_test tests/yield_monitor_test.cpp)
    target_link_libraries(yield_monitor_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME YieldMonitorTest TARGET yield_monitor_test)
    vfp_configure_gui_test(YieldMonitorTest)
endif()

# 定时导出（P1-11 收尾）：到点判定 / 原子落盘 / 保留清理 / 配置往返（不污染用户配置）
if(MSVC)
    add_executable(report_auto_export_test tests/report_auto_export_test.cpp)
    target_link_libraries(report_auto_export_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME ReportAutoExportTest TARGET report_auto_export_test)
    vfp_configure_gui_test(ReportAutoExportTest)
endif()

# 方案加密 / 只读导出（G-P1-10）：AES-256-CTR KAT + 信封 round-trip / 错误口令拒绝 / 篡改拒绝
if(MSVC)
    add_executable(scheme_package_test tests/scheme_package_test.cpp)
    target_link_libraries(scheme_package_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME SchemePackageTest TARGET scheme_package_test)
endif()

# 国际化（G-P1-5）：验证 visionflow_en.qm 加载并翻译关键源文（tr → .ts → .qm 闭环）
if(MSVC)
    add_executable(i18n_test tests/i18n_test.cpp)
    target_link_libraries(i18n_test PRIVATE vfp_core Qt6::Test)
    # 确保 .qm 已生成并部署到运行目录（Qt6::Test 已带入 Qt6::Core）
    add_dependencies(i18n_test VisionFlowPlatform)
    vfp_add_qtest(NAME I18nTest TARGET i18n_test)
endif()

# 数据库跨线程访问测试（QSqlDatabase 线程亲和性 / 每线程独立连接）
if(MSVC)
    add_executable(database_test tests/database_test.cpp)
    target_link_libraries(database_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME DatabaseTest TARGET database_test)
endif()

# 场景编辑性能测试（NFR1.1：撤销快照 / 连线创建 随规模变化）
if(MSVC)
    add_executable(perf_test tests/perf_test.cpp)
    target_link_libraries(perf_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME PerfTest TARGET perf_test)
endif()

# 脚本执行安全策略单元测试（语言白名单 / 解释器隔离 / 环境清理 / 审计 / 总开关）
if(MSVC)
    add_executable(script_security_test tests/script_security_test.cpp)
    target_link_libraries(script_security_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME ScriptSecurityPolicyTest TARGET script_security_test)
endif()

# 出厂默认口令 → 写操作闸（A1-①）：权限收口、改密解锁、方案落盘拒绝
if(MSVC)
    add_executable(factory_password_guard_test tests/factory_password_guard_test.cpp)
    target_link_libraries(factory_password_guard_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME FactoryPasswordGuardTest TARGET factory_password_guard_test)
endif()

# 受限令牌组合探针（诊断用，非测试用例）：把 {去特权} × {管理员SID deny-only} × {低完整性}
# × {受限SID列表} 的组合跑成矩阵，用于定位 STATUS_DLL_INIT_FAILED(0xC0000142) 的成因。
# 纯 Win32、不链接 Qt/vfp_core（避免被测沙箱之外的因素干扰），也不注册为 ctest 用例。
if(MSVC)
    add_executable(restricted_token_probe tests/restricted_token_probe.cpp)
    target_compile_options(restricted_token_probe PRIVATE /utf-8)
endif()

# 通讯回写链路：本机回环模拟 PLC（QTcpServer）→ TCP 客户端 → 「发送数据」算子回写
if(MSVC)
    add_executable(comm_writeback_test tests/comm_writeback_test.cpp)
    target_link_libraries(comm_writeback_test PRIVATE vfp_core Qt6::Test)
    vfp_add_qtest(NAME CommWritebackTest TARGET comm_writeback_test)
endif()

# ---------------------------------------------------------------
# 测试运行环境
# ---------------------------------------------------------------
# 测试可执行文件位于 build/bin/<config>，而 Qt 运行库与插件（platforms /
# sqldrivers）随发行目录 dist/VisionFlowPlatform 部署。若不显式指定，
# Qt 找不到 QSQLITE 驱动 → 数据库测试报 "can not load requested driver"。
if(MSVC)
    set(VFP_DEPLOY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist/VisionFlowPlatform")
    set(VFP_TESTS NodeCoreTest OpencvSerializationTest IntegrationTest DatabaseTest PerfTest
                  ScriptSecurityPolicyTest CommWritebackTest OpencvAngleAreaTest DnnDetectTest CameraIoTest
                  DnnSegmentTest OpencvDefectAlignTest SoakTest StatisticsReportTest SchemePackageTest
                  I18nTest SubFlowTest AnomalyDetectTest NodeExecuteSmokeTest
                  FactoryPasswordGuardTest FeatureMatchTest CalibChainTest)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
        # path_list_prepend 由 CMake 负责插入并处理平台路径分隔符，避免在
        # ENVIRONMENT 值里嵌套分号导致列表被错误拆分
        set_tests_properties(${VFP_TESTS} PROPERTIES
            ENVIRONMENT "QT_PLUGIN_PATH=${VFP_DEPLOY_DIR}"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${VFP_DEPLOY_DIR}")
    else()
        set_tests_properties(${VFP_TESTS} PROPERTIES
            ENVIRONMENT "QT_PLUGIN_PATH=${VFP_DEPLOY_DIR};PATH=${VFP_DEPLOY_DIR}")
    endif()
endif()
