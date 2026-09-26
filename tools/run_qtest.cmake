# QtTest 结果转发包装（修 W-A：ctest 下看不到测试明细）
#
# 根因（2026-09-25 第四轮定位）：丢的不是进程的 stdout，而是 QtTest 的 **plain-text(txt)
# 日志通道**——当 stdout 被重定向（ctest 的管道、`> file`、IDE 输出窗都算）时该通道一条
# 都不写；同一进程的其他输出通道全部正常。同一 exe（node_core_test.exe，13 个 PASS 行 +
# 1 行 Totals）在**同样重定向 stdout** 条件下的对照，可复现：
#
#     ./node_core_test.exe                          -> stdout 0 行 /   0 B
#     ./node_core_test.exe -v1                      -> stdout 0 行 /   0 B   (排除"缓冲没 flush")
#     QT_ASSUME_STDERR_HAS_CONSOLE=1 同上            -> stdout 17 行 / 884 B  (PASS+Totals 全在)
#     QT_FORCE_STDERR_LOGGING=1 同上                 -> stdout 17 行 / 884 B
#     QT_LOGGING_TO_CONSOLE=1 同上（已废弃）          -> stdout 17 行 / 884 B + stderr 142 B 告警
#     ./node_core_test.exe -functions                -> stdout 11 行 / 250 B  (QtTest 的 printf 路径)
#     ./node_core_test.exe -o -,junitxml             -> stdout 21 行 / 1526 B (XML 日志器)
#     ./node_core_test.exe -o -,tap                  -> stdout 19 行 /  452 B (TAP 日志器)
#     ./node_core_test.exe -o <f>,txt                -> 文件   884 B          (写文件通道)
#
# ⇒ 三条结论：① 与 CRT fd1 本身无关（-functions / junitxml / tap / -o 落盘四种取法都能出
# 东西），也与句柄是 file 还是 pipe 无关（四种重定向目标实测全 0 行）；② 与 `vfp_core` 的
# DLL 静态初始化无关——上一版"凡链接 vfp_core 的 exe 丢掉 stdout"的归因不成立，本 Qt 构建
# 里 28 个测试套件**全部**链接 vfp_core，没有"未链接 vfp_core 的 QtTest"可作对照；当时拿
# tests/opencv_nodes_test.exe 的"29 行"作对照也不成立：它不是 QtTest（自写 main + std::printf），
# 只能证明"非 QtTest 程序可以打印"；③ Qt 自己的告警就是机制线索——它说 QT_LOGGING_TO_CONSOLE
# 已废弃、请改用 QT_ASSUME_STDERR_HAS_CONSOLE / QT_FORCE_STDERR_LOGGING，即 Qt 在写 txt 日志
# 前会判断"有没有控制台"，本机构建在判定为"无控制台"时把 txt 规则整个关掉。Qt 内部判定
# 依据（查哪个句柄）本轮未取源码证据。
#
# 本包装保留两件事，都是单设环境变量给不了的：
#   - 把结果落成文件再回读打印，使 `ctest` / `ctest -V` 恒有明细（不依赖每个调用方设环境，
#     也不依赖 Qt 后续版本的路由改动）；子进程的 stdout 因环境修复会自带一份明细，
#     故用 OUTPUT_QUIET 收掉，避免重复打印。
#   - 三道闸：结果里没有 `Totals:` 行 ⇒ 失败；Totals 里 `N failed` > 0 ⇒ 失败；
#     `0 passed` ⇒ 失败。后两道防的是"进程崩溃/整套件被 skip 也显示成一条安静的 PASS"
#     （正是本轮刚犯过的同类错误）。
#   - 判红之前把结果文件另存成 `<套件>.failed-<UTC时间戳>.txt`（见下方 vfp_preserve_failure）：
#     ${VFP_LOG} 每次运行开头就被 file(REMOVE) 覆盖，偶发失败"红一次、重跑就绿"时证据当场消失，
#     观察项（A6 的 IntegrationTest）因此永远无法结案。另存文件只增不删，留在 build/Testing/ 里。
#
# 调用方式见 CMakeLists.txt 的 vfp_add_qtest()。
if(NOT DEFINED VFP_EXE OR VFP_EXE STREQUAL "")
    message(FATAL_ERROR "缺少 -DVFP_EXE=<测试可执行文件>")
endif()
if(NOT DEFINED VFP_LOG OR VFP_LOG STREQUAL "")
    message(FATAL_ERROR "缺少 -DVFP_LOG=<结果文件路径>")
endif()

# VFP_ARGS 由调用方作为 CMake 列表整体塞进一个 -D（见 CMakeLists.txt 的 vfp_add_qtest）。
# 实测：`-DVFP_ARGS=a;b c;d` 到本脚本仍是三元素列表，含空格的单个 argv 完整保住；
# 逐条 `-DVFP_ARGn` 传法同样完整。真正会丢参数的是下面这种按空格重切的写法
# （separate_arguments UNIX_COMMAND 会把 "a b" 拆成两个 argv），故不采用。
set(vfpArgs "")
foreach(vfpArg IN LISTS VFP_ARGS)
    list(APPEND vfpArgs "${vfpArg}")
endforeach()

get_filename_component(vfpLogDir "${VFP_LOG}" DIRECTORY)
file(MAKE_DIRECTORY "${vfpLogDir}")
file(REMOVE "${VFP_LOG}")

set(ENV{QT_ASSUME_STDERR_HAS_CONSOLE} "1")
execute_process(
    COMMAND "${VFP_EXE}" ${vfpArgs} "-o" "${VFP_LOG},txt"
    RESULT_VARIABLE vfpRc
    OUTPUT_QUIET
    TIMEOUT 1800)

if(EXISTS "${VFP_LOG}")
    file(READ "${VFP_LOG}" vfpContent)
    string(REGEX REPLACE "\n$" "" vfpContent "${vfpContent}")
    message("${vfpContent}")
else()
    set(vfpContent "")
endif()

# 判红之前先把结果文件另存一份：${VFP_LOG} 每次运行开头就被 file(REMOVE) 覆盖，
# 而"偶发失败"往往正是红一次、重跑就绿——不另存则证据当场消失，观察项永远无法结案
# （A6 的 IntegrationTest 就是这一形态）。时间戳命名，多次判红互不覆盖。
function(vfp_preserve_failure reason)
    if(NOT EXISTS "${VFP_LOG}")
        return()
    endif()
    string(TIMESTAMP vfpStamp "%Y%m%dT%H%M%SZ" UTC)
    get_filename_component(vfpBase "${VFP_LOG}" NAME_WE)
    set(vfpKept "${vfpLogDir}/${vfpBase}.failed-${vfpStamp}.txt")
    configure_file("${VFP_LOG}" "${vfpKept}" COPYONLY)
    message("保留失败明细：${vfpKept}（${reason}）")
endfunction()

if(NOT vfpContent MATCHES "Totals:")
    vfp_preserve_failure("无 Totals 行")
    message(FATAL_ERROR "退出码 ${vfpRc}，但结果文件里没有 \"Totals:\" 行 ⇒ 用例没跑完"
                        "（启动即崩 / 被杀 / QTest 未接管输出），不记作通过：${VFP_LOG}")
endif()
if(NOT vfpRc EQUAL 0)
    vfp_preserve_failure("QtTest 退出码非 0")
    message(FATAL_ERROR "QtTest 退出码 ${vfpRc}（明细见上，或 ${VFP_LOG}）")
endif()
# QTest 在个别情况下崩溃后退出码仍为 0，故同时以 Totals 行的两个计数为准：
# 有失败判红，一条用例都没通过（全 skip / 全没跑）同样判红。
string(REGEX MATCH "Totals: [^\r\n]*" vfpTotals "${vfpContent}")
if(vfpTotals MATCHES ", [1-9][0-9]* failed")
    vfp_preserve_failure("${vfpTotals}")
    message(FATAL_ERROR "结果里仍有失败计数：${vfpTotals}（明细见上，或 ${VFP_LOG}）")
endif()
if(vfpTotals MATCHES "^Totals: 0 passed")
    vfp_preserve_failure("0 passed")
    message(FATAL_ERROR "没有任何用例通过（多半是整套件被 skip 或用例名失配）：${vfpTotals}")
endif()
