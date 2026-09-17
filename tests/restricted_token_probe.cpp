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
// 用法：
//   restricted_token_probe.exe          跑完整组合矩阵（A–L，含若干对照组）
//   restricted_token_probe.exe -dbg     调试器模式：对 A/B 两组逐条打印 DLL 加载、异常
//                                       与加载器快照（自动开关 IFEO GlobalFlag 并恢复），
//                                       用于定位"进程为什么起不来"。0xC0000142 就是这么
//                                       定位到 KERNELBASE.dll 的 DLL_PROCESS_ATTACH 失败。
// 建议在提权会话里跑（管理员 SID 处于 enabled 状态时，deny-only 才有对比意义）。

#include <windows.h>
#include <userenv.h>
#include <sddl.h>
#include <aclapi.h>

#include <cstdio>
#include <string>
#include <vector>

// AppContainer 相关 API（CreateAppContainerProfile / DeriveAppContainerSidFromAppContainerName /
// GetAppContainerFolderPath）在 userenv.lib；advapi32（ConvertSidToStringSid 等）由默认库带入。
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

constexpr DWORD kWaitMs = 15000;

struct Variant {
    const char *name;
    bool denyAdminSid;     ///< SidsToDisable = {Administrators}
    bool lowIntegrity;     ///< TokenIntegrityLevel = Low
    bool restrictToWorld;  ///< SidsToRestrict = {Everyone}
    // ---- 追加的对照组：用于定位"为什么任何子进程都起不来" ----
    bool denyUsersSid = false;    ///< SidsToDisable={Users}：判断是否 Administrators 特有
    bool denyAbsentSid = false;   ///< SidsToDisable={不在令牌中的合成 SID}：判断 API 调用本身是否有害
    bool windowsCwd = false;      ///< 子进程显式以 C:\Windows 为工作目录
    bool keepPrivileges = false;  ///< 不传 DISABLE_MAX_PRIVILEGE（隔离"去特权"是否是共因）
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

    // 待禁用 SID 列表（最多 3 个：Administrators / Users / 一个不在令牌中的合成 SID）
    BYTE sidBufs[3][SECURITY_MAX_SID_SIZE] = {};
    SID_AND_ATTRIBUTES denySids[3] = {};
    DWORD denyCount = 0;

    if (variant.denyAdminSid) {
        DWORD size = sizeof(sidBufs[0]);
        if (CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, sidBufs[0], &size)) {
            denySids[denyCount].Sid = sidBufs[0];
            ++denyCount;
        }
    }
    if (variant.denyUsersSid) {
        DWORD size = sizeof(sidBufs[1]);
        if (CreateWellKnownSid(WinBuiltinUsersSid, nullptr, sidBufs[1], &size)) {
            denySids[denyCount].Sid = sidBufs[1];
            ++denyCount;
        }
    }
    if (variant.denyAbsentSid) {
        // 合成 S-1-5-21-<随机>：几乎不可能出现在令牌里，用于验证"调用 API 本身"是否有副作用
        SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
        PSID made = nullptr;
        if (AllocateAndInitializeSid(&ntAuthority, 3, 21, 0x5A5A5A5AU, 0x1234U,
                                     0, 0, 0, 0, 0, &made)) {
            const DWORD len = GetLengthSid(made);
            if (len > 0 && len <= sizeof(sidBufs[2])) {
                CopyMemory(sidBufs[2], made, len);
                denySids[denyCount].Sid = sidBufs[2];
                ++denyCount;
            }
            FreeSid(made);
        }
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
    const DWORD tokenFlags = variant.keepPrivileges ? 0 : DISABLE_MAX_PRIVILEGE;
    if (!CreateRestrictedToken(processToken, tokenFlags,
                               denyCount, denyCount ? denySids : nullptr,
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

RunResult runChild(HANDLE token, const std::wstring &command, bool usePipes,
                   const std::wstring &cwd = std::wstring())
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
                                              TRUE, flags, nullptr,
                                              cwd.empty() ? nullptr : cwd.c_str(),
                                              &siex.StartupInfo, &pi);
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

/// 给指定镜像打开/关闭加载器快照（IFEO 的 GlobalFlag，FLG_SHOW_LDR_SNAPS=0x2）。
/// 加载器会把这些诊断经 OutputDebugString 发出，而调试器（本探针）已能收到——
/// 这正是 gflags + 调试器的标准用法，比 DebugView 可靠：子进程早死也不会丢消息。
/// 返回原值：-1 表示原本没有该项（恢复时应删除），-2 表示本函数失败。
long setLoaderSnaps(const std::wstring &imageName, bool enable)
{
    const std::wstring keyPath =
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\"
        + imageName;
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, nullptr, 0,
                        KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return -2;
    }
    DWORD prev = 0;
    DWORD size = sizeof(prev);
    DWORD type = 0;
    const bool hadValue =
        (RegQueryValueExW(key, L"GlobalFlag", nullptr, &type,
                          reinterpret_cast<LPBYTE>(&prev), &size) == ERROR_SUCCESS);
    DWORD flags = hadValue ? prev : 0;
    constexpr DWORD kShowLdrSnaps = 0x00000002;
    if (enable) {
        flags |= kShowLdrSnaps;
    } else {
        flags &= ~kShowLdrSnaps;
    }
    const LONG written = RegSetValueExW(key, L"GlobalFlag", 0, REG_DWORD,
                                       reinterpret_cast<const BYTE *>(&flags), sizeof(flags));
    RegCloseKey(key);
    if (written != ERROR_SUCCESS) {
        return -2;
    }
    return hadValue ? static_cast<long>(prev) : -1;
}

/// 把 GlobalFlag 恢复到调用前的状态（prev 为 setLoaderSnaps 的返回值）。
void restoreLoaderSnaps(const std::wstring &imageName, long prev)
{
    if (prev == -2) {
        return;
    }
    const std::wstring keyPath =
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\"
        + imageName;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, KEY_SET_VALUE, &key)
        != ERROR_SUCCESS) {
        return;
    }
    if (prev < 0) {
        RegDeleteValueW(key, L"GlobalFlag");
    } else {
        DWORD value = static_cast<DWORD>(prev);
        RegSetValueExW(key, L"GlobalFlag", 0, REG_DWORD,
                       reinterpret_cast<const BYTE *>(&value), sizeof(value));
    }
    RegCloseKey(key);
}

// ---------------------------------------------------------------------------
// 调试器模式：用 CreateProcessAsUserW + DEBUG_ONLY_THIS_PROCESS 启动子进程并自己当调试器。
//
// 为什么不用 Process Monitor / DebugView：
//   - ProcMon 只能给出文件/注册表访问，看不到"是哪个 DLL 在初始化阶段失败"；
//   - DebugView 那条路依赖内核捕获，且子进程早死时输出可能丢失；
//   - 调试器模式拿到的是加载器的**真实事件序列**：最后一个成功加载的 DLL 之后的退出状态
//     （0xC0000142 = STATUS_DLL_INIT_FAILED）就是失败点，不需要任何外部工具。
//     再配合加载器快照（见 setLoaderSnaps），可直接看到加载器自己的叙述。
// ---------------------------------------------------------------------------

struct DebugTrace {
    std::vector<std::wstring> lastDlls;       ///< 最后若干个成功加载的 DLL（只留尾部）
    std::vector<std::string> events;          ///< 异常/异常继续等事件
    std::vector<std::string> ldrSnaps;        ///< 加载器快照尾部（开启 GlobalFlag 后才有）
    std::string debugStrings;                 ///< 顺带收 OutputDebugString（简短汇总）
    DWORD exitCode = 0;
    bool started = false;
    bool timedOut = false;
    DWORD lastError = 0;
};

std::string hex32(DWORD value)
{
    char buf[16] = {};
    snprintf(buf, sizeof(buf), "0x%08lX", value);
    return buf;
}

/// 宽字符 → UTF-8。加载器快照全是宽字符串；若用 std::string(w.begin(), w.end())
/// 逐字节截断，输出会变成乱码（本探针第一版就踩了这个）。
std::string toUtf8(const std::wstring &wide)
{
    if (wide.empty()) {
        return std::string();
    }
    const int need = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                         static_cast<int>(wide.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (need <= 0) {
        return std::string();
    }
    std::string out(static_cast<size_t>(need), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                        out.data(), need, nullptr, nullptr);
    return out;
}

std::wstring moduleNameFromHandle(HANDLE hFile, LPVOID base)
{
    wchar_t buf[32768] = {};
    if (hFile) {
        const DWORD len = GetFinalPathNameByHandleW(hFile, buf, 32768, FILE_NAME_NORMALIZED);
        if (len > 0 && len < 32768) {
            return buf;
        }
    }
    if (base) {
        // 退化路径：给不出名字时至少留下基址，便于人工定位
        wchar_t fallback[64] = {};
        swprintf(fallback, 64, L"<unknown @ %p>", base);
        return fallback;
    }
    return L"<unknown>";
}

DebugTrace traceChildUnderDebugger(HANDLE token, const std::wstring &command,
                                   const std::wstring &cwd = std::wstring())
{
    DebugTrace trace;

    std::vector<wchar_t> cmdBuf(command.begin(), command.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const BOOL created = CreateProcessAsUserW(token, nullptr, cmdBuf.data(), nullptr, nullptr,
                                              FALSE,
                                              CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT
                                                  | DEBUG_ONLY_THIS_PROCESS,
                                              nullptr,
                                              cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    if (!created) {
        trace.lastError = GetLastError();
        return trace;
    }
    trace.started = true;

    int timeoutSlices = 0;
    for (;;) {
        DEBUG_EVENT ev{};
        if (!WaitForDebugEvent(&ev, 2000)) {
            if (GetLastError() == ERROR_SEM_TIMEOUT) {
                // 子进程可能停在未处理的异常上：给足时间后强杀，避免探针挂死
                if (++timeoutSlices >= 10) {
                    trace.timedOut = true;
                    TerminateProcess(pi.hProcess, 0);
                    break;
                }
                continue;
            }
            break;
        }

        DWORD continueStatus = DBG_CONTINUE;
        switch (ev.dwDebugEventCode) {
        case LOAD_DLL_DEBUG_EVENT: {
            const std::wstring name = moduleNameFromHandle(ev.u.LoadDll.hFile,
                                                           ev.u.LoadDll.lpBaseOfDll);
            trace.lastDlls.push_back(name);
            if (trace.lastDlls.size() > 10) {
                trace.lastDlls.erase(trace.lastDlls.begin());
            }
            if (ev.u.LoadDll.hFile) {
                CloseHandle(ev.u.LoadDll.hFile);
            }
            break;
        }
        case EXCEPTION_DEBUG_EVENT: {
            const DWORD code = ev.u.Exception.ExceptionRecord.ExceptionCode;
            trace.events.push_back(std::string("exception ") + hex32(code)
                                   + (ev.u.Exception.dwFirstChance ? " (first-chance)"
                                                                   : " (second-chance)"));
            // 初始断点（STATUS_BREAKPOINT）必须放行，否则进程会以 0x80000003 结束；
            // 其它异常交还系统处理，子进程才会以自己的真实状态码退出。
            continueStatus = (code == STATUS_BREAKPOINT) ? DBG_CONTINUE : DBG_EXCEPTION_NOT_HANDLED;
            break;
        }
        case OUTPUT_DEBUG_STRING_EVENT: {
            const auto &ds = ev.u.DebugString;
            std::string utf8;
            if (ds.fUnicode) {
                std::vector<wchar_t> text(ds.nDebugStringLength + 1, L'\0');
                SIZE_T read = 0;
                if (ds.lpDebugStringData && ds.nDebugStringLength > 0
                    && ReadProcessMemory(pi.hProcess, ds.lpDebugStringData, text.data(),
                                         ds.nDebugStringLength * sizeof(wchar_t), &read)) {
                    utf8 = toUtf8(std::wstring(text.data()));
                }
            } else {
                // 加载器快照走的是 ANSI（fUnicode=0）。若忽略该标志按宽字符读，
                // 得到的会是"半个字节"的乱码——本探针第一版就踩了这个。
                std::vector<char> text(ds.nDebugStringLength + 1, '\0');
                SIZE_T read = 0;
                if (ds.lpDebugStringData && ds.nDebugStringLength > 0
                    && ReadProcessMemory(pi.hProcess, ds.lpDebugStringData, text.data(),
                                         ds.nDebugStringLength, &read)) {
                    utf8 = std::string(text.data());
                }
            }
            for (char &ch : utf8) {
                if (ch == '\r' || ch == '\n') {
                    ch = ' ';
                }
            }
            if (!utf8.empty()) {
                // 逐条保留尾部：加载器快照很长，失败点永远在最后
                trace.ldrSnaps.push_back(utf8);
                if (trace.ldrSnaps.size() > 40) {
                    trace.ldrSnaps.erase(trace.ldrSnaps.begin());
                }
                trace.debugStrings += utf8 + " | ";
                if (trace.debugStrings.size() > 400) {
                    trace.debugStrings.resize(400);
                }
            }
            break;
        }
        case EXIT_PROCESS_DEBUG_EVENT:
            trace.exitCode = ev.u.ExitProcess.dwExitCode;
            ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, continueStatus);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return trace;
        default:
            break;
        }
        ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, continueStatus);
    }

    GetExitCodeProcess(pi.hProcess, &trace.exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return trace;
}

void printTrace(const char *label, const DebugTrace &trace)
{
    printf("[dbg] %s\n", label);
    if (!trace.started) {
        printf("    create=FAILED err=%lu (0x%08lX)\n\n", trace.lastError, trace.lastError);
        return;
    }
    printf("    最后成功加载的 DLL（尾部，最多 10 条）:\n");
    for (const std::wstring &dll : trace.lastDlls) {
        printf("      %ls\n", dll.c_str());
    }
    for (const std::string &e : trace.events) {
        printf("    %s\n", e.c_str());
    }
    if (!trace.debugStrings.empty()) {
        printf("    OutputDebugString: %s\n", trace.debugStrings.c_str());
    }
    printf("    exit=0x%08lX%s\n\n", trace.exitCode, trace.timedOut ? " (超时强杀)" : "");
}

// ---------------------------------------------------------------------------
// AppContainer 探针：真正"独立视图"的沙箱（无需独立账户、无需口令）。
//
// 与 deny-only / 受限 SID 的区别：AppContainer 用的是**普通用户令牌 + 容器 SID**，
// 不触碰任何"减弱型"令牌标志，因此不会触发 KERNELBASE 在进程附加阶段失败那条死路。
// 隔离收益：只能访问显式授权的路径 + 自己的容器目录；无网络能力（不给 capabilities）；
// 注册表写入被限制在容器 hive 内。
// ---------------------------------------------------------------------------

RunResult runChildInAppContainer(PSID containerSid, const std::wstring &command)
{
    RunResult result;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) {
        result.lastError = GetLastError();
        return result;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOEXW siex{};
    siex.StartupInfo.cb = sizeof(siex);
    siex.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    siex.StartupInfo.hStdOutput = writeEnd;
    siex.StartupInfo.hStdError = writeEnd;

    SECURITY_CAPABILITIES caps{};
    caps.AppContainerSid = containerSid;
    caps.Capabilities = nullptr;   // 不给任何能力：无 internetClient、无企业认证等
    caps.CapabilityCount = 0;

    SIZE_T attrBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrBytes);
    std::vector<BYTE> attrBuf(attrBytes);
    auto *attrList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    const bool haveAttrs =
        attrBytes > 0 && InitializeProcThreadAttributeList(attrList, 1, 0, &attrBytes)
        && UpdateProcThreadAttribute(attrList, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                                     &caps, sizeof(caps), nullptr, nullptr);
    if (haveAttrs) {
        siex.lpAttributeList = attrList;
    }

    std::vector<wchar_t> cmdBuf(command.begin(), command.end());
    cmdBuf.push_back(L'\0');

    PROCESS_INFORMATION pi{};
    const BOOL created = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT
                                            | (haveAttrs ? EXTENDED_STARTUPINFO_PRESENT : 0),
                                        nullptr, nullptr, &siex.StartupInfo, &pi);
    if (writeEnd) {
        CloseHandle(writeEnd);
    }
    if (haveAttrs) {
        DeleteProcThreadAttributeList(attrList);
    }
    result.started = created != FALSE;
    if (!created) {
        result.lastError = GetLastError();
        CloseHandle(readEnd);
        return result;
    }

    WaitForSingleObject(pi.hProcess, kWaitMs);
    GetExitCodeProcess(pi.hProcess, &result.exitCode);
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
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(readEnd);
    for (char &ch : result.output) {
        if (ch == '\r' || ch == '\n') {
            ch = ' ';
        }
    }
    return result;
}

int runAppContainerProbe()
{
    printf("=== AppContainer 探针 ===\n");

    const wchar_t *profileName = L"VisionFlowPlatform.SandboxProbe";
    PSID sid = nullptr;
    HRESULT hr = CreateAppContainerProfile(profileName, L"VFP Sandbox Probe",
                                          L"VFP script sandbox probe", nullptr, 0, &sid);
    if (FAILED(hr) && hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
        hr = DeriveAppContainerSidFromAppContainerName(profileName, &sid);
    }
    if (FAILED(hr) || !sid) {
        printf("profile 建立失败 hr=0x%08lX（首次创建需要管理员权限）\n",
               static_cast<unsigned>(hr));
        return 1;
    }

    LPWSTR sidString = nullptr;
    ConvertSidToStringSidW(sid, &sidString);
    printf("profile SID: %ls\n", sidString ? sidString : L"(?)");

    PWSTR containerFolder = nullptr;
    if (sidString && SUCCEEDED(GetAppContainerFolderPath(sidString, &containerFolder))
        && containerFolder) {
        printf("容器专属目录: %ls\n", containerFolder);
    }

    wchar_t tempDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempDir);
    const std::wstring userFile = std::wstring(tempDir) + L"vfp_ac_userwrite.txt";
    const std::wstring containerFile =
        containerFolder ? (std::wstring(containerFolder) + L"\\vfp_ac_containerwrite.txt")
                        : std::wstring();

    struct Case {
        const char *label;
        std::wstring command;
    };
    std::vector<Case> cases;
    cases.push_back({ "cmd /c echo（能否启动）", L"cmd.exe /c echo ac-ok" });
    cases.push_back({ "写用户临时目录（应被拒）", L"cmd.exe /c echo x> \"" + userFile + L"\"" });
    if (!containerFile.empty()) {
        cases.push_back({ "写容器专属目录（应成功）",
                          L"cmd.exe /c echo y> \"" + containerFile + L"\"" });
    }
    cases.push_back({ "python -c print（解释器可用性）",
                      L"python -c \"print('ac-py-ok')\"" });

    DeleteFileW(userFile.c_str());
    for (const Case &c : cases) {
        const RunResult r = runChildInAppContainer(sid, c.command);
        printf("    %-34s started=%s exit=0x%08lX out=\"%s\"\n", c.label,
               r.started ? "yes" : "NO", r.exitCode, r.output.c_str());
    }
    printf("    用户临时目录被写入? %s（应为 no）\n",
           GetFileAttributesW(userFile.c_str()) != INVALID_FILE_ATTRIBUTES ? "YES" : "no");
    if (!containerFile.empty()) {
        printf("    容器目录被写入? %s（应为 YES）\n",
               GetFileAttributesW(containerFile.c_str()) != INVALID_FILE_ATTRIBUTES ? "YES" : "no");
    }

    printf("\n    若 python 因读不到安装目录而失败，按需授权（示例，需离线评估后执行）：\n");
    printf("      icacls \"<pythonDir>\" /grant \"*%ls\":(OI)(CI)(RX)\n",
           sidString ? sidString : L"<sid>");
    printf("      icacls \"<pythonDir>\" /remove:g \"*%ls\"   (撤销)\n",
           sidString ? sidString : L"<sid>");
    printf("    删除探针 profile：restricted_token_probe.exe -ac-del\n");

    if (containerFolder) {
        CoTaskMemFree(containerFolder);
    }
    if (sidString) {
        LocalFree(sidString);
    }
    FreeSid(sid);
    return 0;
}

int deleteAppContainerProbeProfile()
{
    const HRESULT hr = DeleteAppContainerProfile(L"VisionFlowPlatform.SandboxProbe");
    printf("DeleteAppContainerProfile hr=0x%08lX\n", static_cast<unsigned>(hr));
    return SUCCEEDED(hr) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 受控矩阵：解释器目录 ACL × 完整性级别
//
// 背景：把 AppContainer 容器 SID 授权到解释器目录后，**Low IL**（受限令牌 + 低完整性）
// 启动解释器会以 0xC0000135 STATUS_DLL_NOT_FOUND 失败；`icacls <dir> /reset` 后恢复。
// 本模式在同一进程里翻转 ACL 并重复测量，直接建立因果关系：
//   干净 → 授权(容器 SID) → 撤销并改授权(普通合成 SID) → 全部撤销
// 每个状态各跑 High IL / Low IL 两格，用来确认"只影响 Low IL"这一特征，
// 并区分"任何外来 ACE 都会引发"与"容器 SID 特有"两种机制。
// ---------------------------------------------------------------------------

/// 解释器目录（PATH 解析 python.exe，取其所在目录）
std::wstring interpreterDir()
{
    wchar_t buf[MAX_PATH] = {};
    const DWORD len = SearchPathW(nullptr, L"python.exe", nullptr, MAX_PATH, buf, nullptr);
    if (len == 0 || len >= MAX_PATH) {
        return std::wstring();
    }
    const std::wstring path(buf);
    const size_t slash = path.find_last_of(L'\\');
    return (slash == std::wstring::npos) ? std::wstring() : path.substr(0, slash);
}

/// 目录 DACL 中是否有该 SID 的允许条目；可选返回 ACE 总数
bool daclHasSid(const std::wstring &dir, PSID sid, DWORD *aceCount = nullptr)
{
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    bool found = false;
    DWORD count = 0;
    if (GetNamedSecurityInfoW(const_cast<LPWSTR>(dir.c_str()), SE_FILE_OBJECT,
                              DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr,
                              &sd) == ERROR_SUCCESS) {
        if (dacl) {
            for (DWORD i = 0; i < dacl->AceCount; ++i) {
                void *ace = nullptr;
                if (!GetAce(dacl, i, &ace) || !ace) {
                    continue;
                }
                ++count;
                auto *header = static_cast<ACE_HEADER *>(ace);
                if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) {
                    continue;
                }
                auto *allowed = static_cast<ACCESS_ALLOWED_ACE *>(ace);
                if (EqualSid(reinterpret_cast<PSID>(&allowed->SidStart), sid)) {
                    found = true;
                }
            }
        }
    }
    if (sd) {
        LocalFree(sd);
    }
    if (aceCount) {
        *aceCount = count;
    }
    return found;
}

/// 追加(grant=true，SET_ACCESS)/撤销(grant=false，REVOKE_ACCESS) 某 SID 的
/// 「读取+执行（含子目录继承）」条目——与产品 grantInterpreterAccess 使用同一套 API
bool setDirAclForSid(const std::wstring &dir, PSID sid, bool grant,
                     DWORD rights = GENERIC_READ | GENERIC_EXECUTE)
{
    if (!sid) {
        return false;
    }
    PACL oldDacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetNamedSecurityInfoW(const_cast<LPWSTR>(dir.c_str()), SE_FILE_OBJECT,
                              DACL_SECURITY_INFORMATION, nullptr, nullptr, &oldDacl, nullptr,
                              &sd) != ERROR_SUCCESS) {
        return false;
    }
    EXPLICIT_ACCESSW ea{};
    ea.grfAccessPermissions = rights;
    ea.grfAccessMode = grant ? SET_ACCESS : REVOKE_ACCESS;
    ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
    ea.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid);

    PACL newDacl = nullptr;
    const DWORD merged = SetEntriesInAclW(1, &ea, oldDacl, &newDacl);
    bool ok = false;
    if (merged == ERROR_SUCCESS && newDacl) {
        ok = SetNamedSecurityInfoW(const_cast<LPWSTR>(dir.c_str()), SE_FILE_OBJECT,
                                   DACL_SECURITY_INFORMATION, nullptr, nullptr, newDacl, nullptr)
             == ERROR_SUCCESS;
    }
    if (newDacl) {
        LocalFree(newDacl);
    }
    if (sd) {
        LocalFree(sd);
    }
    return ok;
}

int runInterpreterAclMatrix()
{
    const std::wstring dir = interpreterDir();
    if (dir.empty()) {
        printf("=== 解释器 ACL 矩阵 ===\n找不到 python.exe（PATH），无法实验\n");
        return 1;
    }
    printf("=== 解释器 ACL × 完整性级别 矩阵 ===\n解释器目录: %ls\n\n", dir.c_str());

    // 容器 SID：优先复用产品 profile，缺失则退回探针 profile
    PSID containerSid = nullptr;
    if ((FAILED(DeriveAppContainerSidFromAppContainerName(L"VisionFlowPlatform.Sandbox",
                                                           &containerSid))
         || !containerSid)
        && (FAILED(DeriveAppContainerSidFromAppContainerName(
                       L"VisionFlowPlatform.SandboxProbe", &containerSid))
            || !containerSid)) {
        printf("两个容器 profile 都不存在，请先跑一次 -ac\n");
        return 1;
    }
    // 对照 SID：一个不在任何令牌里的普通 SID（区分"任何外来 ACE"与"容器 SID 特有"）
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    PSID plainSid = nullptr;
    AllocateAndInitializeSid(&ntAuthority, 3, 21, 0x1BADB002U, 0x7777U, 0, 0, 0, 0, 0, &plainSid);

    const Variant highIl{ "去特权（High IL）", false, false, false };
    const Variant lowIl{ "去特权 + Low IL", false, true, false };
    auto cell = [](const char *label, const Variant &variant) {
        std::string note;
        HANDLE token = createVariantToken(variant, note);
        if (!token) {
            printf("    %-18s token=FAILED %s\n", label, note.c_str());
            return;
        }
        const RunResult r = runChild(token, L"python.exe -I -E -c \"print('il-ok')\"", true);
        printf("    %-18s exit=0x%08lX out=\"%s\"\n", label, r.exitCode, r.output.c_str());
        CloseHandle(token);
    };
    auto phase = [&](const char *title) {
        DWORD aceCount = 0;
        const bool hasContainer = daclHasSid(dir, containerSid, &aceCount);
        const bool hasPlain = daclHasSid(dir, plainSid, nullptr);
        printf("[%s] ACE 总数=%lu 容器SID=%s 对照SID=%s\n", title, aceCount,
               hasContainer ? "yes" : "no", hasPlain ? "yes" : "no");
        cell("High IL", highIl);
        cell("Low IL", lowIl);
        printf("\n");
    };

    // 1) 干净态
    setDirAclForSid(dir, containerSid, false);
    setDirAclForSid(dir, plainSid, false);
    phase("1 干净态");

    // 2) 授权容器 SID（与产品同一 API：SetEntriesInAcl SET_ACCESS）
    setDirAclForSid(dir, containerSid, true);
    phase("2 已授权：容器 SID");

    // 2b) 目录上只给「遍历 + 读属性 + 同步」（不给读写数据）：判断触发点究竟是
    //     "目录上存在容器条目"，还是需要读/执行位。若 traverse-only 安全，即可给出
    //     可用的产品修法（目录给遍历、文件给读写）。
    setDirAclForSid(dir, containerSid, false);
    setDirAclForSid(dir, containerSid, true,
                    FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE);
    phase("2b 容器 SID（目录仅 traverse）");
    setDirAclForSid(dir, containerSid, false);

    // 3) 换成普通对照 SID
    setDirAclForSid(dir, containerSid, false);
    setDirAclForSid(dir, plainSid, true);
    phase("3 已授权：普通对照 SID");

    // 3b) 只授权单个文件（不含目录条目）：判断触发点是"目录上的容器 ACE"还是"文件上的"
    //     ——这一格直接决定产品修法（文件级授权是否可替代目录级授权）
    const std::wstring pyExe = dir + L"\\python.exe";
    setDirAclForSid(pyExe, containerSid, true);
    phase("3b 容器 SID（仅 python.exe 文件）");
    setDirAclForSid(pyExe, containerSid, false);

    // 4) 全部撤销，恢复原状
    setDirAclForSid(dir, plainSid, false);
    phase("4 撤销后（应恢复如初）");

    if (containerSid) {
        FreeSid(containerSid);
    }
    if (plainSid) {
        FreeSid(plainSid);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// AppContainer 文件级授权实验
//
// 动机：-il 矩阵已证明"目录 DACL 上的容器 SID 条目"会让 Low IL 启动解释器失败
// （与权限位无关），而"文件上的条目"无害。于是关键问题变成：
//   容器在**目录没有任何容器条目**、只有文件级授权时，能否遍历目录并跑起解释器？
// 若可以 → 产品授权应改为「目录不加条目 + 文件递归授权」，两种强度即可共存，
//           并且符合最小权限。
// 本模式会递归给解释器目录下的**文件**追加条目（目录一律不碰），跑完容器内实测后
// **自动还原**（同一套递归撤销），保证机器状态不被留下。
// ---------------------------------------------------------------------------

/// 递归对「文件」追加/撤销容器条目（目录一律不碰——这正是本实验要验证的）
void touchFilesRecursive(const std::wstring &root, PSID sid, bool grant, int *touched, int *failed)
{
    const std::wstring pattern = root + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        const std::wstring name(fd.cFileName);
        if (name == L"." || name == L"..") {
            continue;
        }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            continue;   // 跳过符号链接/联接，避免递归成环
        }
        const std::wstring full = root + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            touchFilesRecursive(full, sid, grant, touched, failed);
        } else if (setDirAclForSid(full, sid, grant)) {
            ++(*touched);
        } else {
            ++(*failed);
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

int runAppContainerFileOnlyProbe()
{
    printf("=== AppContainer 文件级授权实验（目录不加容器条目）===\n");
    const std::wstring dir = interpreterDir();
    if (dir.empty()) {
        printf("找不到 python.exe（PATH），无法实验\n");
        return 1;
    }
    printf("解释器目录: %ls\n", dir.c_str());

    PSID containerSid = nullptr;
    if ((FAILED(DeriveAppContainerSidFromAppContainerName(L"VisionFlowPlatform.Sandbox",
                                                           &containerSid))
         || !containerSid)
        && (FAILED(DeriveAppContainerSidFromAppContainerName(
                       L"VisionFlowPlatform.SandboxProbe", &containerSid))
            || !containerSid)) {
        printf("两个容器 profile 都不存在，请先跑一次 -ac\n");
        return 1;
    }

    // 目录上的容器条目先清掉（可能是上一轮实验留下的），确保"目录无容器条目"这一前提成立
    setDirAclForSid(dir, containerSid, false);
    DWORD aceCount = 0;
    const bool dirHasContainer = daclHasSid(dir, containerSid, &aceCount);
    printf("目录容器条目=%s（ACE 总数=%lu）\n", dirHasContainer ? "yes" : "no", aceCount);

    int touched = 0;
    int failed = 0;
    touchFilesRecursive(dir, containerSid, true, &touched, &failed);
    printf("已按文件授权：成功 %d 个，失败 %d 个（目录一律未加容器条目）\n\n", touched, failed);

    // 容器内实测：cmd（系统目录默认对包可读）与 python（依赖解释器目录下的文件）
    struct Case {
        const char *label;
        std::wstring command;
    };
    const Case cases[] = {
        { "cmd /c echo（基线）", L"cmd.exe /c echo ac-file-ok" },
        { "python -c import", L"python.exe -I -E -c \"import json,os;print('ac-file-py-ok')\"" },
    };
    for (const Case &c : cases) {
        const RunResult r = runChildInAppContainer(containerSid, c.command);
        printf("    %-22s started=%s exit=0x%08lX out=\"%s\"\n", c.label,
               r.started ? "yes" : "NO", r.exitCode, r.output.c_str());
    }

    // 自动还原：把刚加的文件条目全部撤销
    int reverted = 0;
    int revertFailed = 0;
    touchFilesRecursive(dir, containerSid, false, &reverted, &revertFailed);
    DWORD afterCount = 0;
    const bool stillHas = daclHasSid(dir, containerSid, &afterCount);
    printf("\n已还原：撤销文件条目 %d 个（失败 %d），目录容器条目=%s（ACE 总数=%lu）\n",
           reverted, revertFailed, stillHas ? "yes" : "no", afterCount);
    printf("判定：若 python 行为 exit=0 且打印 ac-file-py-ok，则「目录不加条目 + 文件级授权」可行。\n");

    FreeSid(containerSid);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    printf("restricted token probe\n");
    printf("current process: elevated=%s\n\n", isElevated() ? "yes" : "no");

    // -ac：AppContainer 探针（独立视图沙箱的候选方案，详见 runAppContainerProbe 注释）
    if (argc > 1 && std::string(argv[1]) == "-ac") {
        return runAppContainerProbe();
    }
    // -ac-del：删除探针创建的 AppContainer profile（清理）
    if (argc > 1 && std::string(argv[1]) == "-ac-del") {
        return deleteAppContainerProbeProfile();
    }
    // -il：解释器目录 ACL × 完整性级别 受控矩阵（定位"授权后 Low IL 启动失败"的因果）
    if (argc > 1 && std::string(argv[1]) == "-il") {
        return runInterpreterAclMatrix();
    }
    // -ac-file：文件级授权实验（目录不加容器条目），跑完自动还原
    if (argc > 1 && std::string(argv[1]) == "-ac-file") {
        return runAppContainerFileOnlyProbe();
    }

    // -dbg：调试器模式。只跑 A（可用基线）与 B（deny-only，全挂），逐条打印 DLL 加载与异常，
    // 用来定位"到底是哪个组件在初始化阶段失败"，不依赖 ProcMon/DebugView。
    if (argc > 1 && std::string(argv[1]) == "-dbg") {
        const Variant cases[] = {
            { "A 去特权（可用基线）", false, false, false },
            { "B A + 管理员SID deny-only", true, false, false },
        };
        // 打开加载器快照：加载器会把内部叙述发给调试器（就是本进程），
        // 从而直接看到"卡在哪个 DLL/哪一步"。用完必须恢复（见下方 restore）。
        const std::wstring targetImage = L"cmd.exe";
        const long prevFlag = setLoaderSnaps(targetImage, true);
        printf("[dbg] loader snaps cmd.exe: prevGlobalFlag=%ld (-1=原本没有该项, -2=设置失败)\n\n",
               prevFlag);

        for (const Variant &variant : cases) {
            std::string note;
            HANDLE token = createVariantToken(variant, note);
            if (!token) {
                printf("[dbg] %s token=FAILED (%s)\n\n", variant.name, note.c_str());
                continue;
            }
            const DebugTrace trace = traceChildUnderDebugger(token, L"cmd.exe /c echo probe-ok");
            char label[256] = {};
            snprintf(label, sizeof(label), "%s   cmd /c echo", variant.name);
            printTrace(label, trace);
            if (!trace.ldrSnaps.empty()) {
                printf("    加载器快照（尾部 %d 条）:\n",
                       static_cast<int>(trace.ldrSnaps.size()));
                for (const std::string &line : trace.ldrSnaps) {
                    printf("      %s\n", line.c_str());
                }
            }
            printf("\n");
            CloseHandle(token);
        }

        restoreLoaderSnaps(targetImage, prevFlag);
        printf("[dbg] loader snaps 已恢复 (prevGlobalFlag=%ld)\n", prevFlag);
        return 0;
    }

    const Variant variants[] = {
        { "A 去特权（当前产品行为）", false, false, false },
        { "B A + 管理员SID deny-only", true, false, false },
        { "C A + 低完整性(Low IL)", false, true, false },
        { "D A + deny-only + Low IL", true, true, false },
        { "E A + 受限SID列表={Everyone}", false, false, true },
        { "F A + deny-only + 受限列表", true, false, true },
        // ---- 定位对照组：回答"为什么任何子进程都起不来" ----
        { "G A + Users deny-only", false, false, false, true, false, false },
        { "H A + 不在令牌中的SID deny-only", false, false, false, false, true, false },
        { "J B + 子进程工作目录 C:\\Windows", true, false, false, false, false, true },
        // 隔离共因：去掉 DISABLE_MAX_PRIVILEGE 后，两种致命组合是否仍致命？
        { "K 保留特权 + 管理员 deny-only", true, false, false, false, false, false, true },
        { "L 保留特权 + 受限列表={Everyone}", false, false, true, false, false, false, true },
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

        // 工作目录对照：默认继承父进程 CWD（E:\halcon\2\xin1）。若"显式给一个肯定可访问的
        // 工作目录"能让子进程活过来，则根因是 CWD 访问被拒，而不是 DLL 本身加载不了。
        if (variant.windowsCwd) {
            const RunResult cwdRun = runChild(token, children[0].command, true, L"C:\\Windows");
            printf("    %-24s started=%s exit=0x%08lX out=\"%s\"\n",
                   "cmd /c echo CWD=C:\\Windows", cwdRun.started ? "yes" : "NO",
                   cwdRun.exitCode, cwdRun.output.c_str());
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
