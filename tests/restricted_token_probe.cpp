// 受限令牌组合探针（诊断用，不参与产品运行）
//
// 背景：ScriptSecurityPolicy 的受限令牌目前只做到「去特权」（DISABLE_MAX_PRIVILEGE）。
// 管理员组 deny-only 与低完整性级别曾被实测为「子进程 0xC0000142 STATUS_DLL_INIT_FAILED」，
// 但当时没有留下可复核的数据。本探针把组合矩阵跑成一张表：
//   令牌组合 × 子进程命令行 → {令牌是否建立, 子进程是否启动, 退出码, 捕获输出}
// 同时打印每个令牌的**实际状态**（剩余特权数 / 管理员 SID 状态 / 完整性级别），
// 以排除「参数位置写错导致其实没生效」这类假结论。
//
// 纯 Win32 实现：不依赖 Qt/vfp_core，因此不受被测沙箱之外的因素干扰。
// 用法：直接运行 build/bin/Release/restricted_token_probe.exe（建议在提权会话里跑，
// 这样管理员 SID 处于 enabled 状态，deny-only 才有对比意义）。

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr DWORD kWaitMs = 15000;

struct Variant {
    const char *name;
    bool denyAdminSid;     ///< SidsToDisable = {Administrators}
    bool lowIntegrity;     ///< TokenIntegrityLevel = Low
    bool restrictToWorld;  ///< SidsToRestrict = {Everyone}
};

struct Child {
    const char *label;
    const wchar_t *command;
};

struct TokenInfo {
    int privilegeCount = -1;
    std::string adminSid = "unknown";
    std::string integrity = "unknown";
};

struct RunResult {
    bool started = false;
    DWORD exitCode = 0;
    DWORD lastError = 0;
    std::string output;
};

bool isElevated()
{
    BOOL elevated = FALSE;
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation{};
        DWORD size = sizeof(elevation);
        if (GetTokenInformation(token, TokenElevation, &elevation, size, &size)) {
            elevated = elevation.TokenIsElevated;
        }
        CloseHandle(token);
    }
    return elevated != FALSE;
}

TokenInfo describeToken(HANDLE token)
{
    TokenInfo info;

    DWORD len = 0;
    GetTokenInformation(token, TokenPrivileges, nullptr, 0, &len);
    if (len > 0) {
        std::vector<BYTE> buf(len);
        if (GetTokenInformation(token, TokenPrivileges, buf.data(), len, &len)) {
            info.privilegeCount = int(reinterpret_cast<TOKEN_PRIVILEGES *>(buf.data())->PrivilegeCount);
        }
    }

    BYTE admin[SECURITY_MAX_SID_SIZE] = {};
    DWORD adminSize = sizeof(admin);
    if (CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admin, &adminSize)) {
        DWORD groupsLen = 0;
        GetTokenInformation(token, TokenGroups, nullptr, 0, &groupsLen);
        if (groupsLen > 0) {
            std::vector<BYTE> buf(groupsLen);
            if (GetTokenInformation(token, TokenGroups, buf.data(), groupsLen, &groupsLen)) {
                auto *groups = reinterpret_cast<TOKEN_GROUPS *>(buf.data());
                info.adminSid = "absent";
                for (DWORD i = 0; i < groups->GroupCount; ++i) {
                    if (EqualSid(groups->Groups[i].Sid, admin)) {
                        info.adminSid = (groups->Groups[i].Attributes & SE_GROUP_USE_FOR_DENY_ONLY)
                                            ? "deny-only"
                                            : "enabled";
                        break;
                    }
                }
            }
        }
    }

    DWORD ilLen = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &ilLen);
    if (ilLen > 0) {
        std::vector<BYTE> buf(ilLen);
        if (GetTokenInformation(token, TokenIntegrityLevel, buf.data(), ilLen, &ilLen)) {
            auto *label = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(buf.data());
            const DWORD rid = *GetSidSubAuthority(label->Label.Sid,
                                                  *GetSidSubAuthorityCount(label->Label.Sid) - 1);
            if (rid <= SECURITY_MANDATORY_LOW_RID) {
                info.integrity = "Low";
            } else if (rid < SECURITY_MANDATORY_MEDIUM_RID) {
                info.integrity = "MediumMinus";
            } else if (rid < SECURITY_MANDATORY_HIGH_RID) {
                info.integrity = "Medium";
            } else {
                info.integrity = "High";
            }
        }
    }
    return info;
}

HANDLE createVariantToken(const Variant &variant, std::string &note)
{
    HANDLE processToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_DEFAULT | TOKEN_ASSIGN_PRIMARY,
                          &processToken)) {
        note = "OpenProcessToken failed";
        return nullptr;
    }

    BYTE adminSid[SECURITY_MAX_SID_SIZE] = {};
    DWORD adminSize = sizeof(adminSid);
    SID_AND_ATTRIBUTES denySid{};
    DWORD denyCount = 0;
    if (variant.denyAdminSid && CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, adminSid, &adminSize)) {
        denySid.Sid = adminSid;
        denyCount = 1;
    }

    BYTE worldSid[SECURITY_MAX_SID_SIZE] = {};
    DWORD worldSize = sizeof(worldSid);
    SID_AND_ATTRIBUTES restrictSid{};
    DWORD restrictCount = 0;
    if (variant.restrictToWorld && CreateWellKnownSid(WinWorldSid, nullptr, worldSid, &worldSize)) {
        restrictSid.Sid = worldSid;
        restrictCount = 1;
    }

    // 参数位置必须核对：deny-only 在第 3/4 位，受限 SID 列表在第 7/8 位（语义完全不同）
    HANDLE restricted = nullptr;
    if (!CreateRestrictedToken(processToken, DISABLE_MAX_PRIVILEGE,
                               denyCount, denyCount ? &denySid : nullptr,
                               0, nullptr,
                               restrictCount, restrictCount ? &restrictSid : nullptr,
                               &restricted)) {
        note = "CreateRestrictedToken failed, err=" + std::to_string(GetLastError());
        CloseHandle(processToken);
        return nullptr;
    }
    CloseHandle(processToken);

    if (variant.lowIntegrity) {
        SID_IDENTIFIER_AUTHORITY authority = SECURITY_MANDATORY_LABEL_AUTHORITY;
        PSID lowSid = nullptr;
        if (AllocateAndInitializeSid(&authority, 1, SECURITY_MANDATORY_LOW_RID,
                                     0, 0, 0, 0, 0, 0, 0, &lowSid)) {
            TOKEN_MANDATORY_LABEL label{};
            label.Label.Sid = lowSid;
            label.Label.Attributes = SE_GROUP_INTEGRITY;
            if (!SetTokenInformation(restricted, TokenIntegrityLevel, &label,
                                     sizeof(label) + GetLengthSid(lowSid))) {
                note += "SetTokenInformation(integrity) failed, err="
                        + std::to_string(GetLastError()) + "; ";
            }
            FreeSid(lowSid);
        }
    }
    return restricted;
}

RunResult runChild(HANDLE token, const std::wstring &command, bool usePipes)
{
    RunResult result;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (usePipes && !CreatePipe(&readEnd, &writeEnd, &sa, 0)) {
        result.lastError = GetLastError();
        return result;
    }
    if (readEnd) {
        SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOEXW siex{};
    siex.StartupInfo.cb = sizeof(STARTUPINFOW);
    DWORD flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;

    HANDLE inheritList[1] = { writeEnd };
    std::vector<BYTE> attrBuf;
    LPPROC_THREAD_ATTRIBUTE_LIST attrList = nullptr;
    if (usePipes) {
        siex.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        siex.StartupInfo.hStdOutput = writeEnd;
        siex.StartupInfo.hStdError = writeEnd;

        SIZE_T attrBytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attrBytes);
        attrBuf.resize(attrBytes);
        attrList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
        if (attrBytes > 0 && InitializeProcThreadAttributeList(attrList, 1, 0, &attrBytes)
            && UpdateProcThreadAttribute(attrList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                         inheritList, sizeof(inheritList), nullptr, nullptr)) {
            siex.StartupInfo.cb = sizeof(siex);
            siex.lpAttributeList = attrList;
            flags |= EXTENDED_STARTUPINFO_PRESENT;
        }
    }

    std::vector<wchar_t> cmdBuf(command.begin(), command.end());
    cmdBuf.push_back(L'\0');

    PROCESS_INFORMATION pi{};
    const BOOL created = CreateProcessAsUserW(token, nullptr, cmdBuf.data(), nullptr, nullptr,
                                              TRUE, flags, nullptr, nullptr, &siex.StartupInfo, &pi);
    if (!created) {
        result.lastError = GetLastError();
    }
    result.started = created != FALSE;

    if (writeEnd) {
        CloseHandle(writeEnd);
    }
    if (attrList) {
        DeleteProcThreadAttributeList(attrList);
    }

    if (result.started) {
        WaitForSingleObject(pi.hProcess, kWaitMs);
        GetExitCodeProcess(pi.hProcess, &result.exitCode);
        if (readEnd) {
            char buf[512];
            DWORD available = 0;
            while (PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
                DWORD got = 0;
                if (!ReadFile(readEnd, buf, sizeof(buf) - 1, &got, nullptr) || got == 0) {
                    break;
                }
                buf[got] = '\0';
                result.output += buf;
                if (result.output.size() > 300) {
                    break;
                }
            }
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    if (readEnd) {
        CloseHandle(readEnd);
    }

    // 保留完整输出（已在上方限制在 300 字节内）：多行信息（如 Python 的 sitecustomize
    // traceback）是判断"能否正常使用"的关键，不能只留首行。
    for (char &ch : result.output) {
        if (ch == '\r') {
            ch = ' ';
        }
        if (ch == '\n') {
            ch = '|';
        }
    }
    return result;
}

} // namespace

int main()
{
    printf("restricted token probe\n");
    printf("current process: elevated=%s\n\n", isElevated() ? "yes" : "no");

    const Variant variants[] = {
        { "A 去特权（当前产品行为）", false, false, false },
        { "B A + 管理员SID deny-only", true, false, false },
        { "C A + 低完整性(Low IL)", false, true, false },
        { "D A + deny-only + Low IL", true, true, false },
        { "E A + 受限SID列表={Everyone}", false, false, true },
        { "F A + deny-only + 受限列表", true, false, true },
    };

    // 写入目标：一个普通用户可写的 Medium 完整性目录。Low IL 子进程应当无法写入，
    // 这正是 Low IL 的隔离收益所在，用一条子命令把它量化出来。
    wchar_t tempDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempDir);
    const std::wstring writeTarget = std::wstring(tempDir) + L"vfp_probe_write.txt";
    const std::wstring writeCommand =
        L"cmd.exe /c echo probe> \"" + writeTarget + L"\"";

    const Child children[] = {
        { "cmd /c echo", L"cmd.exe /c echo probe-ok" },
        { "python -c print", L"python -c \"print('probe-py-ok')\"" },
    };

    for (const Variant &variant : variants) {
        std::string note;
        HANDLE token = createVariantToken(variant, note);
        if (!token) {
            printf("[%s] token=FAILED (%s)\n\n", variant.name, note.c_str());
            continue;
        }

        const TokenInfo info = describeToken(token);
        printf("[%s] token=ok privileges=%d adminSid=%s integrity=%s %s\n",
               variant.name, info.privilegeCount, info.adminSid.c_str(),
               info.integrity.c_str(), note.c_str());

        for (const Child &child : children) {
            const RunResult result = runChild(token, child.command, true);
            if (result.started) {
                printf("    %-24s started=yes exit=0x%08lX out=\"%s\"\n",
                       child.label, result.exitCode, result.output.c_str());
            } else {
                printf("    %-24s started=NO  lastError=%lu (0x%08lX)\n",
                       child.label, result.lastError, result.lastError);
            }
        }

        // 写入测试：Low IL 子进程应无法写入 Medium 完整性的普通用户目录（隔离收益的量化）
        DeleteFileW(writeTarget.c_str());
        const RunResult writeRun = runChild(token, writeCommand, true);
        const bool wrote = GetFileAttributesW(writeTarget.c_str()) != INVALID_FILE_ATTRIBUTES;
        printf("    %-24s started=%s exit=0x%08lX fileWritten=%s out=\"%s\"\n",
               "cmd /c echo>file", writeRun.started ? "yes" : "NO", writeRun.exitCode,
               wrote ? "YES" : "no", writeRun.output.c_str());
        if (wrote) {
            DeleteFileW(writeTarget.c_str());
        }

        // 不带管道跑一次：用于区分「进程/DLL 初始化失败」与「管道/句柄相关失败」
        const RunResult noPipe = runChild(token, children[0].command, false);
        if (noPipe.started) {
            printf("    %-24s started=yes exit=0x%08lX (no pipes)\n",
                   "cmd /c whoami /groups", noPipe.exitCode);
        } else {
            printf("    %-24s started=NO  lastError=%lu (0x%08lX) (no pipes)\n",
                   "cmd /c whoami /groups", noPipe.lastError, noPipe.lastError);
        }
        printf("\n");
        CloseHandle(token);
    }

    // 0xC0000142 提示：STATUS_DLL_INIT_FAILED；0xC0000022：STATUS_ACCESS_DENIED
    printf("legend: exit 0xC0000142 = STATUS_DLL_INIT_FAILED, 0xC0000022 = STATUS_ACCESS_DENIED\n");
    return 0;
}
