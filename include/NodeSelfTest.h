#pragma once

/// 节点自检：遍历注册表全部节点，实例化 + init + 空输入 run
/// 结果写入 exe 目录 selftest_nodes_report.txt
/// 返回 0 = 全部正常；1 = 存在失败
int runNodeSelfTest();
