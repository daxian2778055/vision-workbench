# QtTest 结果转发包装（修 W-A：ctest 下看不到任何测试输出）
#
# 现象（2026-09-25 实测）：链接了 vfp_core 的测试 exe 把自己的 stdout 整个丢掉——
# 直接跑、`> file` 重定向、QtTest 的 `-o -,txt` 三种方式 stdout 都是 0 行，只有 QTest
# 自己的 `-o <file>,<fmt>` 落盘通道能拿到结果；未链接 vfp_core 的 `opencv_nodes_test`
# 同样的跑法却有 29 行输出。两个 exe 的 PE 子系统都是 console(3)，故与 WIN32_EXECUTABLE 无关。
# 根因本轮未定位（不在用例可断言的范围内，故只绕开不追）。
#
# 绕法：用 cmake.exe 当中间进程——被测 exe 的结果写进文件，本文件再把内容打到自己的
# stdout（这条通道 ctest 收得到），并按原始退出码决定成败。这样 `ctest --output-on-failure`
# 与 `ctest -V` 都能看到 `PASS/FAIL/Totals` 明细，门禁变红时可定位。
#
# 额外一道闸：结果文件里没有 `Totals:` 行就判失败。进程启动即崩 / 被外部杀掉时退出码
# 不一定非 0，而"没有跑完"绝不能算通过。
#
# 调用方式见 CMakeLists.txt 的 vfp_add_qtest()。
if(NOT DEFINED VFP_EXE OR VFP_EXE STREQUAL "")
    message(FATAL_ERROR "缺少 -DVFP_EXE=<测试可执行文件>")
endif()
if(NOT DEFINED VFP_LOG OR VFP_LOG STREQUAL "")
    message(FATAL_ERROR "缺少 -DVFP_LOG=<结果文件路径>")
endif()

# 命令行 -D 传列表会塌成字符串，这里显式拆开，保证每个参数各自是一个 argv
set(vfpArgs "")
if(DEFINED VFP_ARGS AND NOT VFP_ARGS STREQUAL "")
    separate_arguments(vfpArgs UNIX_COMMAND "${VFP_ARGS}")
endif()

get_filename_component(vfpLogDir "${VFP_LOG}" DIRECTORY)
file(MAKE_DIRECTORY "${vfpLogDir}")
file(REMOVE "${VFP_LOG}")

execute_process(
    COMMAND "${VFP_EXE}" ${vfpArgs} "-o" "${VFP_LOG},txt"
    RESULT_VARIABLE vfpRc
    TIMEOUT 1800)

if(EXISTS "${VFP_LOG}")
    file(READ "${VFP_LOG}" vfpContent)
    string(REGEX REPLACE "\n$" "" vfpContent "${vfpContent}")
    message("${vfpContent}")
else()
    set(vfpContent "")
endif()

if(NOT vfpContent MATCHES "Totals:")
    message(FATAL_ERROR "退出码 ${vfpRc}，但结果文件里没有 \"Totals:\" 行 ⇒ 用例没跑完"
                        "（启动即崩 / 被杀 / QTest 未接管输出），不记作通过：${VFP_LOG}")
endif()
if(NOT vfpRc EQUAL 0)
    message(FATAL_ERROR "QtTest 退出码 ${vfpRc}（明细见上，或 ${VFP_LOG}）")
endif()
# QTest 在个别情况下崩溃后退出码仍为 0，故同时以 Totals 行的失败计数为准（0 才放行）
string(REGEX MATCH "Totals: [^\r\n]*" vfpTotals "${vfpContent}")
if(vfpTotals MATCHES ", [1-9][0-9]* failed")
    message(FATAL_ERROR "结果里仍有失败计数：${vfpTotals}（明细见上，或 ${VFP_LOG}）")
endif()
