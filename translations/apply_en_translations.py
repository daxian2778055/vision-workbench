# 一次性工具：把 lupdate 生成的 visionflow_en.ts 中的 <source> 填入英文翻译。
# 用法：python apply_en_translations.py
# 维护：新增 tr() 后先 `lupdate src ui -ts visionflow_en.ts`，再补本脚本的 EN 字典并重跑。
import re
import os

HERE = os.path.dirname(os.path.abspath(__file__))
TS = os.path.join(HERE, "visionflow_en.ts")

# 源文本（未转义）-> 英文。覆盖 lupdate 抽取的全部 186 条。
EN = {
    # ExecutionStatusController
    "继续": "Continue",
    "暂停": "Pause",
    "继续执行（不清输入缓存）": "Continue (keep input buffer)",
    "暂停当前流程（不清输入缓存，再按继续）": "Pause current flow (keep input buffer; press Continue to resume)",
    "执行开始": "Execution started",
    "执行停止": "Execution stopped",
    "连续运行中...": "Running continuously...",
    "执行完成": "Execution completed",
    "执行错误: %1": "Execution error: %1",
    "已请求暂停（等当前节点跑完）": "Pause requested (waiting for current node to finish)",
    "已暂停": "Paused",
    "已继续": "Resumed",
    # FlowExecutor
    "Flow scene is not set": "Flow scene is not set",
    "流程中存在循环连接，无法确定执行顺序。": "The flow contains a cyclic connection; execution order cannot be determined.",
    "Error executing node %1: %2": "Error executing node %1: %2",
    # MainWindow（.ui 与 .cpp）
    "Vision Flow Platform": "Vision Flow Platform",
    "工具箱": "Toolbox",
    "Tool Library": "Tool Library",
    "流程编辑": "Flow Editor",
    "图像显示": "Image Display",
    "参数面板": "Parameter Panel",
    "日志": "Log",
    "文件": "File",
    "编辑": "Edit",
    "视图": "View",
    "通讯": "Communication",
    "系统": "System",
    "语言": "Language",
    "New": "New",
    "Save": "Save",
    "Load": "Load",
    "Exit": "Exit",
    "Add Flow": "Add Flow",
    "Delete Flow": "Delete Flow",
    "保存方案": "Save Scheme",
    "打开方案": "Open Scheme",
    "Chinese": "Chinese",
    "English": "English",
    "开始执行": "Start Execution",
    "停止执行": "Stop Execution",
    "显示/隐藏工具箱": "Show/Hide Toolbox",
    "显示/隐藏参数面板": "Show/Hide Parameter Panel",
    "显示/隐藏图像显示": "Show/Hide Image Display",
    "显示/隐藏日志": "Show/Hide Log",
    "设计模式": "Design Mode",
    "运行模式": "Run Mode",
    "设备管理": "Device Management",
    "接收事件": "Received Events",
    "发送事件": "Sent Events",
    "心跳管理": "Heartbeat Management",
    "数据监视": "Data Monitor",
    "全局触发": "Global Trigger",
    "全局变量": "Global Variables",
    "MVS相机配置": "MVS Camera Configuration",
    "用户管理": "User Management",
    "配方管理": "Recipe Management",
    "报警历史": "Alarm History",
    "检测结果": "Inspection Results",
    "结果表": "Results Table",
    "参数查找": "Parameter Search",
    "算子代码导出": "Export Operator Code",
    "操作日志": "Operation Log",
    "统计报表": "Statistics Report",
    "失败时停止流程": "Stop Flow on Failure",
    "运行界面设计": "Runtime UI Design",
    "警告：流程名 \"%1\" 已存在，触发路由可能被重定向": "Warning: flow name \"%1\" already exists; trigger routing may be redirected",
    "安全提示": "Security Notice",
    "管理员账户 admin 仍在使用出厂默认口令，任何人都可登录并修改方案。\n\n请通过「系统 → 用户管理」立即修改密码。":
        "The admin account is still using the factory default password; anyone can log in and modify the scheme.\n\nPlease change the password immediately via \"System -> User Management\".",
    "简体中文": "Simplified Chinese",
    "新建方案": "New Scheme",
    "有流程执行线程超时未退出，已取消新建方案": "A flow execution thread timed out and did not exit; creating a new scheme was cancelled",
    "提示": "Hint",
    "有流程仍在执行且未能在限定时间内退出，已取消新建方案以避免程序崩溃。": "Flows are still running and did not exit within the time limit; creating a new scheme was cancelled to avoid a crash.",
    "保存项目: %1": "Save project: %1",
    "项目保存成功: %1": "Project saved successfully: %1",
    "项目保存失败: %1": "Failed to save project: %1",
    "已创建分组「%1」（%2 个算子）": "Created group \"%1\" (%2 operators)",
    "创建分组：%1（%2 个算子）": "Create group: %1 (%2 operators)",
    "请先在画布上选中至少 2 个算子，再创建分组": "Please select at least 2 operators on the canvas before creating a group",
    "已解散 %1 个分组（组内算子已保留）": "Dissolved %1 group(s); operators inside are kept",
    "解散分组：%1 个": "Dissolve group: %1",
    "请先选中要解散的分组框（点分组标题栏）": "Please select the group box to dissolve (click its title bar)",
    "请先选中要折叠/展开的分组框（点分组标题栏）": "Please select the group box to collapse/expand (click its title bar)",
    "已折叠/展开 %1 个分组": "Collapsed/Expanded %1 group(s)",
    "折叠/展开分组：%1 个": "Collapse/Expand group: %1",
    "请先选中要复制的算子": "Please select the operator to copy first",
    "复制失败：没有可复制的算子": "Copy failed: no operator to copy",
    "已复制 %1 个算子": "Copied %1 operator(s)",
    "（%1 条跨边界的连线未包含）": "(%1 cross-boundary connection(s) not included)",
    "粘贴失败：%1": "Paste failed: %1",
    "当前没有流程": "No flow currently",
    "流程处于编辑锁定状态（运行中），无法插入片段": "The flow is edit-locked (running); cannot insert snippet",
    "插入片段失败：%1": "Failed to insert snippet: %1",
    "已插入 %1 个算子": "Inserted %1 operator(s)",
    "请先选中要导出的算子": "Please select the operator to export first",
    "导出失败：没有可导出的算子": "Export failed: no operator to export",
    "导出方案片段": "Export Scheme Snippet",
    "方案片段 (*%1)": "Scheme Snippet (*%1)",
    "无法写入文件：\n%1\n%2": "Cannot write file:\n%1\n%2",
    "写入不完整：\n%1": "Incomplete write:\n%1",
    "已导出片段：%1（%2 个算子）": "Exported snippet: %1 (%2 operators)",
    "；%1 条跨边界连线未包含": "; %1 cross-boundary connection(s) not included",
    "只读方案：禁止覆盖保存（可另存为新方案）": "Read-only scheme: overwriting is forbidden (you may save as a new scheme)",
    "语言设置将在重启后生效。": "Language setting takes effect after restart.",
    "导出加密/只读方案": "Export Encrypted/Read-only Scheme",
    "导出加密/只读方案…": "Export Encrypted/Read-only Scheme...",
    "当前没有可导出的方案。": "No scheme available to export.",
    "加密（打开时需口令）": "Encrypted (requires password to open)",
    "只读（分发后禁止覆盖保存）": "Read-only (overwriting forbidden after distribution)",
    "加密：": "Encryption:",
    "口令：": "Password:",
    "确认口令：": "Confirm password:",
    "只读：": "Read-only:",
    "加密必须设置口令。": "Encryption requires a password.",
    "两次输入的口令不一致。": "The two password entries do not match.",
    "加密方案 (*.vfpe);;方案文件 (*.vfp)": "Encrypted scheme (*.vfpe);;Scheme file (*.vfp)",
    "加密": "Encrypted",
    "未加密": "Not encrypted",
    "+只读": "+Read-only",
    "已导出%1方案：%2": "Exported %1 scheme: %2",
    "导出失败：\n%1": "Export failed:\n%1",
    "导入方案片段": "Import Scheme Snippet",
    "无法读取文件：\n%1\n%2": "Cannot read file:\n%1\n%2",
    "导入片段：%1": "Imported snippet: %1",
    "已自动保存（异常退出后可恢复）": "Auto-saved (recoverable after abnormal exit)",
    "定时导出失败：%1": "Scheduled export failed: %1",
    "已定时导出 %1 个文件到 %2": "Scheduled export wrote %1 file(s) to %2",
    "（清理旧报告 %1 个）": "(cleaned up %1 old report(s))",
    " ⚠ 记录数触顶（仅覆盖最近 %1 条），报表可能不完整（建议调小统计小时数）": " ⚠ Record limit reached (only the most recent %1 kept); report may be incomplete (consider reducing the statistics hour count)",
    "定时导出部分失败：%1": "Scheduled export partially failed: %1",
    "上次异常退出留下的恢复文件已损坏，无法恢复，已清除": "The recovery file from the last abnormal exit is corrupted and cannot be restored; it has been cleared",
    "时间未知": "Time unknown",
    "未命名方案（从未保存过）": "Unsaved scheme (never saved)",
    "崩溃恢复": "Crash Recovery",
    "检测到上次运行未正常退出，存在自动保存的内容：": "Detected that the last run did not exit normally; auto-saved content exists:",
    "原方案：%1\n自动保存时间：%2\n\n是否恢复到界面？\n（选择「丢弃」将删除该恢复文件）": "Original scheme: %1\nAuto-save time: %2\n\nRestore to the interface?\n(Choosing \"Discard\" deletes the recovery file)",
    "恢复": "Restore",
    "丢弃": "Discard",
    "已丢弃上次异常退出留下的自动保存内容": "Discarded the auto-saved content from the last abnormal exit",
    "恢复失败：无法读取恢复文件。\n%1": "Restore failed: cannot read the recovery file.\n%1",
    "已从自动保存内容恢复（原方案：%1；自动保存时间：%2）。请确认后用「保存」写回方案文件": "Recovered from auto-saved content (original scheme: %1; auto-save time: %2). Please confirm and use \"Save\" to write back to the scheme file",
    "未保存的修改": "Unsaved Changes",
    "%1 前，当前方案有未保存的修改：": "%1 ago, the current scheme had unsaved changes:",
    "方案：%1\n\n「保存并继续」写回方案文件；「不保存继续」将丢弃这些修改。": "Scheme: %1\n\n\"Save and Continue\" writes back to the scheme file; \"Continue without Saving\" discards these changes.",
    "保存并继续": "Save and Continue",
    "不保存继续": "Continue without Saving",
    "取消": "Cancel",
    "退出前确认": "Confirm Before Exit",
    "方案有未保存的修改：": "The scheme has unsaved changes:",
    "方案：%1\n\n「保存并退出」写回方案文件；「不保存退出」将丢弃这些修改，自动保存内容也会一并清除。": "Scheme: %1\n\n\"Save and Exit\" writes back to the scheme file; \"Exit without Saving\" discards these changes and also clears the auto-saved content.",
    "保存并退出": "Save and Exit",
    "不保存退出": "Exit without Saving",
    "加载项目: %1": "Load project: %1",
    "打开加密方案": "Open Encrypted Scheme",
    "该方案已加密，请输入口令：": "This scheme is encrypted; please enter the password:",
    "项目加载失败：口令错误或文件损坏（当前方案保持不变）": "Failed to load project: wrong password or corrupted file (current scheme unchanged)",
    "项目加载失败：文件不存在、格式损坏或内容为空（当前方案保持不变）": "Failed to load project: file not found, corrupted, or empty (current scheme unchanged)",
    "有流程执行线程超时未退出，已取消打开方案": "A flow execution thread timed out and did not exit; opening the scheme was cancelled",
    "有流程仍在执行且未能在限定时间内退出，已取消打开方案以避免程序崩溃。": "Flows are still running and did not exit within the time limit; opening the scheme was cancelled to avoid a crash.",
    "项目加载成功": "Project loaded successfully",
    "节点 %1 执行成功": "Node %1 executed successfully",
    "节点 %1 执行失败": "Node %1 failed to execute",
    "请在图像上拖拽绘制搜索区域（右键取消）": "Drag on the image to draw the search region (right-click to cancel)",
    "ROI 已写入 %1 参数": "ROI written to %1 parameter",
    # MainWindow 第二轮：可见控件标签/提示/菜单（原先硬编码 QStringLiteral）
    "流程控制": "Flow Control",
    "单次执行": "Single Run",
    "以软触发方式运行当前流程一次": "Run the current flow once via software trigger",
    "单步": "Step",
    "逐节点执行：每运行一个算子后暂停（可配合单次执行排查流程）": "Execute node by node: pause after each operator (use with Single Run to debug the flow)",
    "暂停当前流程（不清输入缓存）；再按继续": "Pause current flow (keep input cache); press Continue to resume",
    "连续模式": "Continuous Mode",
    "软触发模式": "Software Trigger Mode",
    "硬触发模式": "Hardware Trigger Mode",
    "帮助(&H)": "Help (&H)",
    "使用手册(&M)": "User Manual (&M)",
    "关于": "About",
    "关于 VisionFlowPlatform": "About VisionFlowPlatform",
    "图像来源: 无": "Image Source: None",
    "变量": "Variables",
    "性能统计": "Performance Statistics",
    "创建分组": "Create Group",
    "把当前选中的算子框成一组（纯视觉容器，不影响执行）": "Group the selected operators (visual container only; does not affect execution)",
    "解散分组": "Dissolve Group",
    "删除选中的分组框，组内算子保留": "Remove the selected group box; operators inside are kept",
    "折叠/展开分组": "Collapse/Expand Group",
    "把选中的分组折叠成一条标题栏（纯显示，不影响执行）": "Collapse the selected group into a title bar (display only; does not affect execution)",
    "复制所选算子": "Copy Selected Operators",
    "粘贴": "Paste",
    "导出片段…": "Export Snippet...",
    "把选中的算子（含内部连线）导出为片段文件，供别的方案插入": "Export selected operators (including internal connections) to a snippet file for insertion into other schemes",
    "导入片段…": "Import Snippet...",
    "把片段文件插入到当前流程（放在当前视图中心）": "Insert a snippet file into the current flow (placed at the center of the current view)",
    "定义为子流程…": "Define as Subflow...",
    "把选中的算子（≥2，单进单出、不与成员外连线）定义为命名子流程，供「子流程」算子按名调用；一处修改、所有引用同步生效": "Define the selected operators (>=2, single input/output, no connections outside the selection) as a named subflow, called by name by the 'SubFlow' operator; edit once and every reference follows",
    "删除子流程定义…": "Delete Subflow Definition...",
    "删除一个子流程定义；画布算子原样保留，引用它的「子流程」算子执行时将报「未定义」": "Delete a subflow definition; the operators on the canvas are kept, and 'SubFlow' operators referencing it will report 'undefined' at runtime",
    "定义为子流程": "Define Subflow",
    "子流程名称：": "Subflow name:",
    "已定义子流程「%1」（%2 个算子）。用「子流程」算子按此名称调用。": "Subflow '%1' defined (%2 operators). Call it by this name with the 'SubFlow' operator.",
    "定义子流程：%1": "Define subflow: %1",
    "定义子流程失败：%1": "Failed to define subflow: %1",
    "当前流程没有子流程定义": "The current flow has no subflow definitions",
    "删除子流程定义": "Delete Subflow Definition",
    "选择要删除的子流程（画布算子会保留）：": "Select the subflow to delete (operators on the canvas are kept):",
    "已删除子流程定义「%1」": "Subflow definition '%1' deleted",
    "删除子流程定义：%1": "Delete subflow definition: %1",

    # ProjectManager
    "保存项目": "Save Project",
    "方案文件 (*.vfp)": "Scheme file (*.vfp)",
    "只读方案": "Read-only scheme",
    "当前方案为只读，禁止覆盖原文件。如需修改，请另选路径保存为新方案。": "The current scheme is read-only; overwriting the original file is forbidden. To modify, save to a new path as a new scheme.",
    "保存方案": "Save Scheme",
    "方案已保存:\n%1": "Scheme saved:\n%1",
    "方案保存失败，请确认目标路径可写后重试:\n%1": "Failed to save scheme; please ensure the target path is writable and retry:\n%1",
    "加载项目": "Load Project",
    # QObject（FlowSnippet / PortConnectivity）
    "片段为空": "Snippet is empty",
    "不是方案片段（kind 不匹配，可能贴错了内容）": "Not a scheme snippet (kind mismatch; possibly wrong content pasted)",
    "片段版本不支持：%1（本程序支持到 %2）": "Snippet version not supported: %1 (this program supports up to %2)",
    "片段里没有任何算子": "The snippet contains no operators",
    "片段第 %1 个算子不是有效对象": "Snippet operator #%1 is not a valid object",
    "片段第 %1 个算子缺少类型信息（typeId/nodeType 均无）": "Snippet operator #%1 is missing type info (neither typeId nor nodeType)",
    "片段第 %1 个算子无法创建（类型未注册或参数不兼容），已取消插入": "Snippet operator #%1 could not be created (type unregistered or parameters incompatible); insertion cancelled",
    "剪贴板为空": "Clipboard is empty",
    "剪贴板内容不是合法的方案片段（JSON 解析失败：%1）": "Clipboard content is not a valid scheme snippet (JSON parse failed: %1)",
    "类型不匹配：输出为 [%1]，输入需要 [%2]": "Type mismatch: output is [%1], input requires [%2]",
    "端口无效": "Invalid port",
    "请从输出端口连接到输入端口": "Connect from an output port to an input port",
    "不可连接同一算子的输出与输入": "Cannot connect an operator's output to its own input",
    "该输入端口已连接，请先断开": "This input port is already connected; please disconnect it first",
}


def unescape(s):
    return s.replace("&quot;", "\"").replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">")


def escape(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


with open(TS, "r", encoding="utf-8") as f:
    content = f.read()

blocks = re.findall(r"<message>.*?</message>", content, re.DOTALL)
unmatched = []
new_blocks = []
for b in blocks:
    m = re.search(r"<source>(.*?)</source>", b, re.DOTALL)
    src_raw = m.group(1)
    src = unescape(src_raw)
    if src in EN:
        trans = EN[src]
        # 仅替换翻译节点；保留 <location> 等不变
        nb = re.sub(r"<translation[^>]*>.*?</translation>",
                    "<translation>%s</translation>" % escape(trans), b, count=1, flags=re.DOTALL)
        new_blocks.append(nb)
    else:
        unmatched.append(src_raw)
        new_blocks.append(b)

content = content
idx = 0
for b in blocks:
    content = content.replace(b, new_blocks[idx], 1)
    idx += 1

with open(TS, "w", encoding="utf-8") as f:
    f.write(content)

print("messages:", len(blocks))
print("translated:", len(blocks) - len(unmatched))
print("unmatched:", len(unmatched))
for u in unmatched:
    print("  UNMATCHED:", repr(u))
