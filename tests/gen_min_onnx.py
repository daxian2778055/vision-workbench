"""生成最小 ONNX 分类模型（2 层 MLP：3x224x224 -> 2 类），用于验证
OpenCV dnn 的 ONNX 加载/推理链路。用法：python gen_min_onnx.py <输出路径>"""
import onnx
from onnx import helper, TensorProto
import numpy as np
import sys

out = sys.argv[1] if len(sys.argv) > 1 else "min_model.onnx"

W1 = np.random.RandomState(0).randn(8, 150528).astype(np.float32) * 0.01
B1 = np.zeros(8, dtype=np.float32)
W2 = np.random.RandomState(1).randn(2, 8).astype(np.float32) * 0.1
B2 = np.zeros(2, dtype=np.float32)

nodes = [
    helper.make_node("Flatten", ["input"], ["flat"], axis=1),
    helper.make_node("Gemm", ["flat", "W1", "B1"], ["h"], alpha=1.0, beta=1.0, transB=1),
    helper.make_node("Relu", ["h"], ["hr"]),
    helper.make_node("Gemm", ["hr", "W2", "B2"], ["out"], alpha=1.0, beta=1.0, transB=1),
]
graph = helper.make_graph(
    nodes, "min_mlp",
    [helper.make_tensor_value_info("input", TensorProto.FLOAT, [1, 3, 224, 224])],
    [helper.make_tensor_value_info("out", TensorProto.FLOAT, [1, 2])],
    initializer=[
        helper.make_tensor("W1", TensorProto.FLOAT, W1.shape, W1.flatten().tolist()),
        helper.make_tensor("B1", TensorProto.FLOAT, B1.shape, B1.tolist()),
        helper.make_tensor("W2", TensorProto.FLOAT, W2.shape, W2.flatten().tolist()),
        helper.make_tensor("B2", TensorProto.FLOAT, B2.shape, B2.tolist()),
    ],
)
model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 13)])
onnx.checker.check_model(model)
onnx.save(model, out)
print("saved:", out)
