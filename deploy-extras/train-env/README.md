# 新机准备（训练机）

> 这个文件夹只给**训练机**用。产线机不需要装任何东西（主程序包自带运行时）。
> 最后更新：2026-10-29

## 0. 先分清两台机器

| 机器 | 装什么 | 用来干什么 |
|---|---|---|
| **产线机** | 只装 `VisionFlowPlatform` 主程序包 | 推理、跟踪、计数、报警 |
| **训练机**（一台就够，可多人共用） | 主程序包 **+ 本文件夹的 Python 训练环境** | 标注、训练 YOLO、导出 ONNX |

产线机**永远不要**装 Python/PyTorch：多一层依赖就多一类现场故障，而产线机常年不该动。

## 1. 训练机需要什么

| 项 | 要求 | 说明 |
|---|---|---|
| Windows | 10/11 x64 | — |
| Python | **3.10.x（64 位）** | 3.8–3.10 都行；**不要用 3.12+**（torch 轮子与 YOLOX 都未必齐） |
| PyTorch | **2.x CPU 版** | 训练机没有 NVIDIA 卡就用 CPU 版；有卡可换 CUDA 版（见 §4） |
| YOLOX | 源码（git 或 zip） | 我们只调用它的训练脚本，不修改它 |
| 磁盘 | ≥ 10 GB 空闲 | 环境约 3 GB + 数据集 + 权重 |

## 2. 一键安装（有网）

```powershell
# 在本文件夹里执行（会创建 .venv 并装依赖）
powershell -ExecutionPolicy Bypass -File .\install-train-env.ps1 -PythonExe "C:\Python310\python.exe"

# 验证
.\.venv\Scripts\python.exe -c "import torch, torchvision, numpy, cv2; print(torch.__version__, numpy.__version__)"
```

## 3. 离线安装（产线/无网环境用）

在有网的机器上先把轮子下好（拷到 U 盘带过去）：

```powershell
# 目标机是 Python 3.10 x64 时按下面跑（win_amd64 / cp310）
powershell -ExecutionPolicy Bypass -File .\download-wheels.ps1 -PythonVersion 3.10
```

产物在 `wheels\` 下，整包拷到训练机的本文件夹里，再执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\install-train-env.ps1 -PythonExe "C:\Python310\python.exe" -Offline
```

## 4. 有 NVIDIA 卡想用 GPU

```powershell
# 把 CPU 版 torch 换掉（cu121 为例；驱动需匹配）
.\.venv\Scripts\python.exe -m pip install --force-reinstall torch torchvision ^
  --index-url https://download.pytorch.org/whl/cu121
```

训练命令里把 `-d 0`（device=GPU 0）打开；CPU 版只能 `-d -1`。

## 5. 装 YOLOX

```powershell
# A) 有 git
git clone https://github.com/Megvii-BaseDetection/YOLOX.git C:\YOLOX
# B) 无 git：下载 zip 解压到 C:\YOLOX
#    然后装它自身的依赖（注意：不要装它的 apex/torch 版本约束）
.\.venv\Scripts\python.exe -m pip install -r C:\YOLOX\requirements.txt
.\.venv\Scripts\python.exe -m pip install -v -e C:\YOLOX
```

> **许可提醒**：YOLOX 是 **Apache-2.0**，可商用闭源。
> 若你改用 Ultralytics 的 YOLOv8/v11，那是 **AGPL-3.0**，闭源分发须买 Enterprise 授权；
> 且导出的 ONNX 要选"无 objectness"布局（我们节点默认那个）。

## 6. 与我们主程序的对接

1. 主程序 → 系统 →「检测框标注…」：标注数据集（写出 `图片同名.txt`，YOLO 格式）
2. 主程序 → 系统 →「训练」：选数据集、填 epochs/imgsz、点开始（需要本环境已装好）
3. 训练完成 → 导出 ONNX（界面会给出命令，也可自己跑）：
   ```powershell
   .\.venv\Scripts\python.exe -m yolox.tools.export_onnx -f exps\default\yolox_s.py ^
       -c runs\...\best_ckpt.pth --output-name best.onnx --opset 12 --input-size 640,640
   ```
4. 主程序 → 文件 →「打包模型与依赖…」：把 `best.onnx` 与 `classes.txt` 收进方案
5. 拷方案到产线机 → 仿真跑通 → 上线

## 7. 常见问题

| 现象 | 原因 |
|---|---|
| `pip install torch` 卡在下载 | 走默认源慢：脚本已指定 `download.pytorch.org/whl/cpu` |
| `import torch` 报 DLL 缺失 | 缺 VC++ 运行库：装 vc_redist.x64.exe |
| 训练报 `CUDA out of memory` | 调小 `-b`（batch）或 `--input-size` |
| 训练不收敛 | 数据太少/类别不均衡：先加样本或用我们标注器多标几张 |
| 主程序里训练按钮是灰的 | 环境没装或 Python 路径没配对（主程序会在提示里说明） |
