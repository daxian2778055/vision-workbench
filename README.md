# VisionFlowPlatform

机器视觉流程平台：Qt 6 桌面端，HALCON 24.11-Progress-Steady 与 OpenCV（thirdparty/opencv）
算子库；流程编排、执行、检测记录与项目管理实现在 `src/` 与 `include/`。
文档在 `docs/`，自托管门禁与驱动件在 `tools/`。

## 本机构建与测试

三步与 `tools/ci.ps1` 的现行步骤一致：

```powershell
cmake -S . -B build -G 'Visual Studio 17 2022' -A x64
cmake --build build --config Release -- /m /v:minimal
ctest -C Release --output-on-failure
```

测试执行依赖 `dist/VisionFlowPlatform` 下的 Qt 6 运行期环境；直接跑 exe 提示缺 DLL 时，
借 ctest 或前置 PATH/QT_PLUGIN_PATH。

## 文档入口

- `docs/quick-start-guide.md` —— 快速上手
- `docs/对标差距推进计划.md` —— 台账
- `docs/对标差距推进计划-登记附表.md` —— 活跃登记册（每轮改动与实测读数的落笔位）
- `tools/ci.ps1` —— 自托管 CI 步随行面（构建、ctest、名册与门禁）

## 安全基线（读我）

- 管理员账户出厂口令 admin/admin 仅为出厂种子：首次登录强制弹改密对话框（放弃即退出本次
  登录），改密成功前写闸禁止保存/导出方案、用户增删改与新一次流程运行发起（闸挂在落库层
  与运行发起点，状态栏常驻提示原因；正在跑的流程不受影响，改密后一切恢复）。
- 方案加密信封：PBKDF2-HMAC-SHA256 派生 + AES-256-CTR；信封进口对非本版行情迭代数
  O(1) 拒收不做派生；MAC 校验走恒时比较；账户口令 KDF 同为 PBKDF2（token 自报轮数，
  读侧封顶）。无口令明文导出（176 行起）带随机盐与 HMAC 完整性位，防误毁/漏改/盲改
  的静态校验，不防知情者重签（需更高强度走口令信封）；旧明文 JSON 与旧版打包载入
  时各加注一句提示重新导出。
- 服务端通信缺省绑定全部网卡、到达面无链路鉴权，现场隔离网段是默认前提；该取舍与
  绑指定网卡/鉴权选项的拍板项登记在活跃附表。

## 关于本文件

README 只记实测事实；规程与设计细节以 `docs/` 内文档为准。
本文件补位于 2026-10-18——此前 docs/ 内多处指路语提到的说明书一直缺席，落点自此补齐。
