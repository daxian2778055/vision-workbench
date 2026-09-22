"""生成最小 ONNX 语义分割模型： [1,3,64,64] -> Conv3x3(1 通道, 核全 1/27) -> Sigmoid -> [1,1,64,64]
用于验证 OpenCV dnn 的**分割**推理链路（DnnSegmentNode 的端到端用例）。

设计：卷积核 = 3x3x3 全 1/27（等价于"对 3 通道求均值再 3x3 平滑"），故
    亮区（归一化后 → 1.0）卷积结果 > 0，暗区 = 0；
节点侧默认 useSigmoid + binaryThresh 0.5 ⇒ sigmoid(v) > 0.5 ⟺ v > 0，
所以掩膜恰好等于"输入亮区外扩 1 像素"，判定不依赖浮点细微差异（0 → 恰好 0.5 → 不算前景）。

用法：python gen_min_seg_onnx.py <输出路径>
"""
import sys

import numpy as np
import onnx
from onnx import TensorProto, helper

out = sys.argv[1] if len(sys.argv) > 1 else "min_seg.onnx"

W = np.full((1, 3, 3, 3), 1.0 / 27.0, dtype=np.float32)
B = np.zeros(1, dtype=np.float32)

nodes = [
    helper.make_node("Conv", ["input", "W", "B"], ["conv"],
                     kernel_shape=[3, 3], pads=[1, 1, 1, 1], strides=[1, 1]),
    helper.make_node("Sigmoid", ["conv"], ["out"]),
]
graph = helper.make_graph(
    nodes, "min_seg",
    [helper.make_tensor_value_info("input", TensorProto.FLOAT, [1, 3, 64, 64])],
    [helper.make_tensor_value_info("out", TensorProto.FLOAT, [1, 1, 64, 64])],
    initializer=[
        helper.make_tensor("W", TensorProto.FLOAT, W.shape, W.flatten().tolist()),
        helper.make_tensor("B", TensorProto.FLOAT, B.shape, B.tolist()),
    ],
)
model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 13)])
onnx.checker.check_model(model)
onnx.save(model, out)
print("saved:", out)
