# 构建期刷新 Version.h：把"配置期"的 git 短哈希/日期换成"本次构建时"的。
#
# 为什么必须有这一步（2026-10-23 复核 P1-3）：configure_file 只在 configure 时求值一次，
# 而 ci.ps1 默认**复用既有配置**（日志里的 "reusing existing configuration in build"），
# 只有 -Clean 才重配。于是"提交了代码 → 增量重编 → 打 tag → 打包"会让 exe 里嵌着上一次
# configure 时的提交——manifest 声称的提交与实际二进制不符，现场拿它复现问题必然对不上。
# 该轮在树上实测到：build/generated/Version.h 写 ce6968a，而 HEAD 已是 24b8127。
#
# 单一数据源：正文模板只此一份（cmake/Version.h.in），本脚本与 configure 期的
# configure_file 都从它产出。曾经把正文内嵌在本脚本里，两份定义漂移过一次
# （注释不同 ⇒ 每次 configure 后首次构建多重编一次 MainWindow.cpp，且只改 .in 不生效）。
#
# 用法：cmake -DHEADER=<path> -DTEMPLATE=<tpl> -DREPO=<repo> -DVERSION=<ver>
#            [-DGIT=<git exe>] -P RefreshVersionHeader.cmake
# 内容不变则不重写（不碰时间戳），避免每次构建都触发 MainWindow.cpp 重编。

if(NOT DEFINED GIT)
    set(GIT "git")
endif()

set(_hash "unknown")
execute_process(
    COMMAND "${GIT}" -C "${REPO}" rev-parse --short HEAD
    OUTPUT_VARIABLE _hash
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    set(_hash "unknown")
endif()

string(TIMESTAMP _date "%Y-%m-%d")

# 从唯一模板产出（替换语义与 configure_file 的 @ONLY 一致）
file(READ "${TEMPLATE}" _content)
string(REPLACE "@PROJECT_VERSION@" "${VERSION}" _content "${_content}")
string(REPLACE "@VFP_GIT_HASH@" "${_hash}" _content "${_content}")
string(REPLACE "@VFP_BUILD_DATE@" "${_date}" _content "${_content}")

if(EXISTS "${HEADER}")
    file(READ "${HEADER}" _old)
    if(_old STREQUAL _content)
        return()   # 内容一致：不写文件，避免无谓重编
    endif()
endif()

file(WRITE "${HEADER}" "${_content}")
message(STATUS "Version.h refreshed: ${VERSION} / ${_hash} / ${_date}")
