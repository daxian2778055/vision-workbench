# 构建期刷新 Version.h：把"配置期"的 git 短哈希/日期换成"本次构建时"的。
#
# 为什么必须有这一步（2026-10-23 复核 P1-3）：configure_file 只在 configure 时求值一次，
# 而 ci.ps1 默认**复用既有配置**（日志里的 "reusing existing configuration in build"），
# 只有 -Clean 才重配。于是"提交了代码 → 增量重编 → 打 tag → 打包"会让 exe 里嵌着上一次
# configure 时的提交——manifest 声称的提交与实际二进制不符，现场拿它复现问题必然对不上。
# 该轮在树上实测到：build/generated/Version.h 写 ce6968a，而 HEAD 已是 24b8127。
#
# 用法：cmake -DHEADER=<path> -DREPO=<repo> -DVERSION=<ver> [-DGIT=<git exe>]
#            -P RefreshVersionHeader.cmake
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

# 与 cmake/Version.h.in 的产出保持逐字一致（configure 期 bootstrap 与构建期刷新同一格式）
set(_content "#pragma once

// 由 CMake 生成，请勿手改（改 cmake/Version.h.in 或 CMakeLists.txt 的 project VERSION）。
// 哈希与日期在**每次构建**时刷新（cmake/RefreshVersionHeader.cmake，经 vfp_version_header
// 目标），不是 configure 时的一次性快照——否则复用既有配置的增量构建会嵌进旧提交。
// 作用：让程序内「关于」框能说清装的是哪一版（版本号 + 提交短哈希 + 构建日期）。
#define VFP_VERSION_STRING \"${VERSION}\"
#define VFP_GIT_HASH \"${_hash}\"
#define VFP_BUILD_DATE \"${_date}\"

// 供 About 框直接使用的整串（字面量拼接，可交给 QStringLiteral）
#define VFP_VERSION_DISPLAY \"VisionFlowPlatform \" VFP_VERSION_STRING \" (\" VFP_GIT_HASH \" / \" VFP_BUILD_DATE \")\"
")

if(EXISTS "${HEADER}")
    file(READ "${HEADER}" _old)
    if(_old STREQUAL _content)
        return()   # 内容一致：不写文件，避免无谓重编
    endif()
endif()

file(WRITE "${HEADER}" "${_content}")
message(STATUS "Version.h refreshed: ${VERSION} / ${_hash} / ${_date}")
