"""生成最小 ONNX **实例分割**模型（YOLO-Seg 双输出形态），用于 DnnSegmentNode 实例路径端到端。

图结构：
  · 输入 [1,3,64,64]
  · out1（掩膜原型）= Conv(3→4, 3×3, pad 1, stride 1)
      核 0 = 全 1/27（等价"3 通道均值 + 3×3 平滑"）→ 亮区为正、暗区为 0
      核 1..3 = 0（其余通道恒 0，用来验证"系数选哪个原型通道"确实生效）
      输出形状 [1,4,64,64] → K=4、mh=mw=64（与输入同分辨率，缩放系数 1）
  · out0（框 + 类分数 + 掩膜系数）= Conv(3→10, 4×4, stride 4, **零核** + 偏置) → [1,10,16,16]
      → ReduceMean(axes=[3]) → [1,10,16]（R = 4 + nc + K = 4+2+4 = 10、N = 16）
      偏置 = [cx=32, cy=32, w=16, h=16, cls0=0.90, cls1=0.10, coeff0=1, 0, 0, 0]
      ⇒ 每个锚点都是"框 (24,24,16,16)、类别 0 置信 0.9、掩膜系数 [1,0,0,0]"，靠 NMS 收敛成 1 个实例。
说明一：out0 **不能是常量 initializer 直接当输出**——实测 cv::dnn 报
        "can't find layer for output name"，故这里用"零核卷积 + 偏置 → 均值"造出同值张量，
        既真正依赖输入（可被 importer 正常构图），数值又完全可控。
说明二：掩膜判据 coeffs·protos > 0 与 Ultralytics 的 "sigmoid 后 > 0.5" 数学等价（严格单调），
        故预期掩膜 = "亮区外扩 1 像素" ∩ "框 (24,24,16,16)" = 7×7 ≈ 49 像素。

用法：python gen_min_seg_inst_onnx.py <输出路径>
"""
import sys

import numpy as np
import onnx
from onnx import TensorProto, helper

out = sys.argv[1] if len(sys.argv) > 1 else "min_seg_inst.onnx"

NC, K, N = 2, 4, 16
R = 4 + NC + K  # 10

# --- 原型分支：Conv 3→4，只有 0 号核非零 ---
W = np.zeros((K, 3, 3, 3), dtype=np.float32)
W[0] = 1.0 / 27.0
B = np.zeros(K, dtype=np.float32)

# --- 系数分支：零核卷积 + 偏置 → 每个锚点同值；再由 ReduceMean 压掉空间维得到 [1,R,N] ---
CW = np.zeros((R, 3, 4, 4), dtype=np.float32)          # 零核：输出 = 偏置（与位置无关）
CB = np.zeros(R, dtype=np.float32)
CB[0] = 24 + 8          # cx
CB[1] = 24 + 8          # cy
CB[2] = 16              # w
CB[3] = 16              # h
CB[4] = 0.90            # cls0
CB[4 + 1] = 0.10        # cls1
CB[4 + NC] = 1.0        # 掩膜系数 0（配合原型 0 通道的亮区）

nodes = [
    helper.make_node("Conv", ["input", "W", "B"], ["proto"],
                     kernel_shape=[3, 3], pads=[1, 1, 1, 1], strides=[1, 1]),
    helper.make_node("Conv", ["input", "CW", "CB"], ["detect4d"],
                     kernel_shape=[4, 4], pads=[0, 0, 0, 0], strides=[4, 4]),
    # 注意：opset 13 的 ReduceMean 只收 1 个输入，axes 是**属性**（axes 变输入是 opset 18）
    helper.make_node("ReduceMean", ["detect4d"], ["detect"], axes=[3], keepdims=0),
]
graph = helper.make_graph(
    nodes, "min_yolo_seg",
    [helper.make_tensor_value_info("input", TensorProto.FLOAT, [1, 3, 64, 64])],
    [helper.make_tensor_value_info("detect", TensorProto.FLOAT, [1, R, N]),
     helper.make_tensor_value_info("proto", TensorProto.FLOAT, [1, K, 64, 64])],
    initializer=[
        helper.make_tensor("W", TensorProto.FLOAT, W.shape, W.flatten().tolist()),
        helper.make_tensor("B", TensorProto.FLOAT, B.shape, B.tolist()),
        helper.make_tensor("CW", TensorProto.FLOAT, CW.shape, CW.flatten().tolist()),
        helper.make_tensor("CB", TensorProto.FLOAT, CB.shape, CB.tolist()),
    ],
)
model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 13)])
onnx.checker.check_model(model)
onnx.save(model, out)
print("saved:", out)
