"""U-32 可重放探针：§3.32 表 1 的 ①②③⑥ 四段读数（推进计划「登记未修」第 6 条给的补法）。

为什么要它：§3.32 表 1 那四段是当轮取证探针打的读数，探针腿没入库就被本轮的五条判据腿替代了，
而 build/ 是 gitignore 的 ⇒ 复核者拿不到一条可重跑的命令。这个探针把那段读数变成入库可跑的东西，
而且**同时对两个 revision 跑**：

  head ＝工作树的 src/CalibrationManager.cpp（U-27 修法之后）
  pre  ＝git show d373957:src/CalibrationManager.cpp（U-27 改前那两行实质）

两边各编一个只链 Qt6::Core 的小 exe（一个翻译单元，不动 vfp_core、不动产线字节），打印同一批读数，
再逐条核对期望值（表 1 的①在 pre 侧应看到「九个字符串进表 9 项全 0.00」，在 head 侧应看到它被拒）。
所以它既有读数、也有牙：任何一侧的期望不成立就是 rc=1。

跑法（仓库根目录）：
    python tools/probes/U32_u27_load_side_probe.py
返回码：0＝两侧读数全部命中期望；1＝某条期望不成立；2＝环境／构建起不来（缺 Qt、缺 cmake 等）。

⚠️ 输出全 ASCII：本仓控制台是 cp936，中文经管道会花；SEC 号与台账里的中文段名一一对应
（① ＝SEC1 载入侧项数与元素、②＝SEC2 写侧返回值、③＝SEC3 同键冲突后果链、⑥＝SEC6 导出再装入往返）。
"""
import argparse
import hashlib
import os
import pathlib
import re
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

PRE_SHA = "d373957"          # U-27 的改前基线（§3.32 表 1 取证那一版）
CPP = "src/CalibrationManager.cpp"
HDR = "include/CalibrationManager.h"

PROBE_CPP = r'''// U-32 探针的读数打印器（由 tools/probes/U32_u27_load_side_probe.py 写出，不入库为产线代码）。
// 只用两个 revision 都存在的公开接口（fromJson 单参），所以同一份 .cpp 能同时编到 head 与 pre 两侧。
#include "CalibrationManager.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <cstdio>

namespace {

QJsonArray nums(int n)
{
    QJsonArray a;
    for (int i = 0; i < n; ++i)
        a.append(1.0 + i);
    return a;
}

QVector<double> vec(int n)
{
    QVector<double> v;
    for (int i = 0; i < n; ++i)
        v.append(1.0 + i);
    return v;
}

QString csv(const QVector<double> &v)
{
    QStringList parts;
    for (double d : v)
        parts << QString::number(d, 'f', 2);
    return parts.join(QLatin1Char(','));
}

void dump(const char *sec, const QString &key, CalibrationManager *cm)
{
    const QVector<double> v = cm->homography(key);
    std::printf("%s %-13s inTable=%d size=%2d values=[%s]\n", sec,
                key.toLatin1().constData(), cm->hasHomography(key) ? 1 : 0,
                int(v.size()), csv(v).toLatin1().constData());
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    CalibrationManager *cm = CalibrationManager::instance();

    // SEC1 = 表 1 的 ①：一次载入 13 个键，项数与元素类型一起看
    const int sizes[] = {0, 3, 5, 6, 7, 8, 9, 10, 11, 20};
    QJsonObject o;
    QStringList order;
    for (int n : sizes) {
        const QString k = QStringLiteral("p_n%1").arg(n);
        o[k] = nums(n);
        order << k;
    }
    QJsonArray strings9;
    const char *tok[] = {"fx", "fy", "cx", "cy", "k1", "k2", "p1", "p2", "rms"};
    for (const char *t : tok)
        strings9 << QJsonValue(QString::fromLatin1(t));
    o[QStringLiteral("p_strings9")] = strings9;
    order << QStringLiteral("p_strings9");

    QJsonArray nullbool9;
    nullbool9 << 520.0 << QJsonValue(QJsonValue::Null) << 320.0 << 240.0
              << 0.0 << 0.0 << 0.0 << 0.0 << true;
    o[QStringLiteral("p_nullbool9")] = nullbool9;
    order << QStringLiteral("p_nullbool9");

    o[QStringLiteral("p_scalar")] = 6.0;
    order << QStringLiteral("p_scalar");

    std::printf("SEC1 keys=%d\n", int(o.keys().size()));
    cm->clear();
    cm->fromJson(o);
    for (const QString &k : order)
        dump("SEC1", k, cm);

    // SEC2 = 表 1 的 ②：空表上对 0/3/5/6/.../20 项各写一次（都是新键，无同键冲突）
    cm->clear();
    for (int n : sizes) {
        const QString k = QStringLiteral("w_n%1").arg(n);
        const bool ok = cm->setHomography(k, vec(n));
        std::printf("SEC2 n=%2d setHomography=%d\n", n, ok ? 1 : 0);
    }

    // SEC3 = 表 1 的 ③：宽载荷先经载入进表，随后正常教学写 9 元内参写不写得进
    cm->clear();
    QJsonObject wide;
    wide[QStringLiteral("cam_params")] = nums(20);
    cm->fromJson(wide);
    const int afterLoad = int(cm->homography(QStringLiteral("cam_params")).size());
    const bool teach = cm->setHomography(QStringLiteral("cam_params"), vec(9));
    std::printf("SEC3 afterLoad=%d teach9=%d afterTeach=%d\n", afterLoad, teach ? 1 : 0,
                int(cm->homography(QStringLiteral("cam_params")).size()));

    // SEC6 = 表 1 的 ⑥：导出再装入的往返（表里的短载荷会不会原样回来）
    cm->clear();
    cm->setHomography(QStringLiteral("r_short3"), vec(3));
    cm->setHomography(QStringLiteral("r_empty0"), vec(0));
    const QJsonObject exported = cm->toJson();
    cm->fromJson(exported);
    std::printf("SEC6 exportedKeys=%d backInTable=%d\n", int(exported.keys().size()),
                (cm->hasHomography(QStringLiteral("r_short3"))
                 || cm->hasHomography(QStringLiteral("r_empty0"))) ? 1 : 0);
    dump("SEC6", QStringLiteral("r_short3"), cm);
    dump("SEC6", QStringLiteral("r_empty0"), cm);
    cm->clear();
    return 0;
}
'''

CMAKE = '''cmake_minimum_required(VERSION 3.16)
project(u32_cm_side_probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC OFF)

find_package(Qt6 REQUIRED COMPONENTS Core)

foreach(side head pre)
    add_executable(u32_probe_${side} probe_main.cpp src_${side}/CalibrationManager.cpp)
    target_include_directories(u32_probe_${side} PRIVATE src_${side})
    target_link_libraries(u32_probe_${side} PRIVATE Qt6::Core)
endforeach()
'''


def md5_bytes(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()


def run(cmd, cwd=None, env=None) -> subprocess.CompletedProcess:
    print("  RUN: %s" % " ".join(str(c) for c in cmd))
    return subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")


def read_cache(path: pathlib.Path, key: str) -> str:
    text = path.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"^%s[:=].*?[=;](.+)$" % re.escape(key), text, re.MULTILINE)
    if not m:
        m = re.search(r"^%s=([^\n]+)$" % re.escape(key), text, re.MULTILINE)
    return m.group(1).strip().replace("\\", "/") if m else ""


def parse(lines):
    """把探针 stdout 收成 {SEC: {label: 整行}}，供期望值核对。"""
    out = {}
    for ln in lines:
        parts = ln.split()
        if not parts or not parts[0].startswith("SEC"):
            continue
        sec = parts[0]
        if sec == "SEC1" and parts[1].startswith("keys="):
            out.setdefault(sec, {})["keys"] = parts[1]
        elif sec == "SEC2":
            m = re.search(r"n=\s*\d+", ln)
            out.setdefault(sec, {})[m.group(0).replace(" ", "") if m else ln] = ln
        elif sec == "SEC6" and parts[1].startswith("exported"):
            out.setdefault(sec, {})["summary"] = ln
        elif sec == "SEC3":
            out.setdefault(sec, {})["summary"] = ln
        else:
            out.setdefault(sec, {})[parts[1]] = ln
    return out


def field(line: str, name: str) -> str:
    m = re.search(r"%s=\s*(\S+)" % re.escape(name), line)
    return m.group(1) if m else ""


def expectations(side: str):
    """每个 SEC 的期望读数；pre 侧那两条非数值载荷进表，head 侧被拒。"""
    strings9_in = side == "pre"
    exp = []
    exp.append(("C1", "SEC1 keys=13（一次载入 13 个键）", lambda d: d["SEC1"]["keys"] == "keys=13"))
    for n in (0, 3, 5):
        exp.append(("C2", "SEC1 p_n%d 不进表（少于 6 项）" % n,
                    lambda d, n=n: field(d["SEC1"]["p_n%d" % n], "inTable") == "0"))
    for n in (6, 7, 8, 9, 10, 11, 20):
        exp.append(("C3", "SEC1 p_n%d 进表且 %d 项逐位不变" % (n, n),
                    lambda d, n=n: field(d["SEC1"]["p_n%d" % n], "inTable") == "1"
                    and field(d["SEC1"]["p_n%d" % n], "size") == str(n)
                    and field(d["SEC1"]["p_n%d" % n], "values") ==
                        "[" + ",".join("%.2f" % (1.0 + i) for i in range(n)) + "]"))
    exp.append(("C4", "SEC1 p_scalar（值根本不是数组）不进表",
                lambda d: field(d["SEC1"]["p_scalar"], "inTable") == "0"))
    exp.append(("C5", "SEC1 p_strings9 %s" % ("进表 9 项全 0.00（改前）" if strings9_in
                                              else "被拒、不进表（改后）"),
                lambda d: (field(d["SEC1"]["p_strings9"], "inTable") == "1") == strings9_in))
    exp.append(("C6", "SEC1 p_nullbool9 %s" % ("进表 9 项（null/bool 降成 0.00，改前）" if strings9_in
                                                else "被拒、不进表（改后）"),
                lambda d: (field(d["SEC1"]["p_nullbool9"], "inTable") == "1") == strings9_in))
    exp.append(("C7", "SEC2 空表上 10 个项数各写一次，setHomography 一律返回 1（写侧没有项数白名单）",
                lambda d: all(field(v, "setHomography") == "1" for k, v in d["SEC2"].items())))
    exp.append(("C8", "SEC3 20 项宽载荷进表 → 随后 9 元教学被拒 → 表内仍是 20 项（同键覆盖冲突闸）",
                lambda d: field(d["SEC3"]["summary"], "afterLoad") == "20"
                and field(d["SEC3"]["summary"], "teach9") == "0"
                and field(d["SEC3"]["summary"], "afterTeach") == "20"))
    exp.append(("C9", "SEC6 导出 2 个键（3 项与 0 项）→ 装回后两条都不在表里",
                lambda d: field(d["SEC6"]["summary"], "exportedKeys") == "2"
                and field(d["SEC6"]["summary"], "backInTable") == "0"))
    return exp


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=".")
    ap.add_argument("--workdir", default="build/u32_probe/cm_side")
    ap.add_argument("--generator", default="")
    ap.add_argument("--qt-prefix", default="")
    ap.add_argument("--cmake", default="")
    args = ap.parse_args()

    repo = pathlib.Path(args.repo).resolve()
    cache = repo / "build/CMakeCache.txt"
    work = repo / args.workdir

    cmake_exe = args.cmake or (read_cache(cache, "CMAKE_COMMAND") if cache.exists() else "")
    if not cmake_exe or not pathlib.Path(cmake_exe).exists():
        print("RC=2 找不到 cmake（用 --cmake 指到 cmake.exe）")
        return 2
    generator = args.generator or (read_cache(cache, "CMAKE_GENERATOR") if cache.exists() else "")
    qt_prefix = args.qt_prefix.rstrip("/")
    if not qt_prefix and cache.exists():
        core_dir = read_cache(cache, "Qt6Core_DIR")
        if core_dir:
            qt_prefix = str(pathlib.PurePosixPath(core_dir).parents[2])
    if not qt_prefix or not pathlib.Path(qt_prefix).exists():
        print("RC=2 找不到 Qt6 前缀（用 --qt-prefix 指到 D:/Qt/<ver>/msvc2022_64）")
        return 2

    # 两侧源：head＝工作树那一份，pre＝git show <PRE_SHA>
    head_files = {}
    for rel in (CPP, HDR):
        data = (repo / rel).read_bytes()
        head_files[rel] = data
        print("  head %s md5=%s lines=%d" % (rel, md5_bytes(data), data.count(b"\n")))
    pre_files = {}
    for rel in (CPP, HDR):
        r = subprocess.run(["git", "-C", str(repo), "show", "%s:%s" % (PRE_SHA, rel)],
                           capture_output=True)
        if r.returncode != 0:
            print("RC=2 git show %s:%s 失败：%s" % (PRE_SHA, rel, r.stderr.decode("utf-8", "replace")))
            return 2
        pre_files[rel] = r.stdout
        print("  pre  %s md5=%s lines=%d" % (rel, md5_bytes(r.stdout), r.stdout.count(b"\n")))

    work.mkdir(parents=True, exist_ok=True)
    (work / "probe_main.cpp").write_text(PROBE_CPP, encoding="utf-8", newline="\n")
    (work / "CMakeLists.txt").write_text(CMAKE, encoding="utf-8", newline="\n")
    for side, bundle in (("head", head_files), ("pre", pre_files)):
        d = work / ("src_%s" % side)
        d.mkdir(exist_ok=True)
        (d / "CalibrationManager.cpp").write_bytes(bundle[CPP])
        (d / "CalibrationManager.h").write_bytes(bundle[HDR])

    bdir = work / "b"
    cfg = run([cmake_exe, "-S", str(work), "-B", str(bdir)]
              + (["-G", generator] if generator else [])
              + ["-DCMAKE_PREFIX_PATH=%s" % qt_prefix])
    if cfg.returncode != 0:
        print(cfg.stdout[-2000:])
        print(cfg.stderr[-2000:])
        print("RC=2 configure 失败")
        return 2
    bld = run([cmake_exe, "--build", str(bdir), "--config", "Release"])
    if bld.returncode != 0:
        print(bld.stdout[-4000:])
        print("RC=2 build 失败")
        return 2

    env = dict(os.environ)
    env["PATH"] = str(pathlib.PurePosixPath(qt_prefix) / "bin") + os.pathsep + env.get("PATH", "")
    red_total = 0
    for side in ("pre", "head"):
        exe = bdir / "Release" / ("u32_probe_%s.exe" % side)
        if not exe.exists():
            print("RC=2 产物不在预期位置：%s" % exe)
            return 2
        r = subprocess.run([str(exe)], capture_output=True, env=env,
                           text=True, encoding="utf-8", errors="replace")
        lines = [ln for ln in r.stdout.splitlines() if ln.strip()]
        print("\n===== %s 侧读数（%s · %s）=====" % (side, PRE_SHA if side == "pre" else "工作树",
                                                 md5_bytes((pre_files if side == "pre" else head_files)[CPP])))
        for ln in lines:
            print("  " + ln)
        data = parse(lines)
        red = []
        for cid, desc, fn in expectations(side):
            try:
                ok = fn(data)
            except KeyError as e:
                ok = False
                desc += "（读数缺 %s）" % e
            print("  CHECK %s %s :: %s" % (cid, "OK " if ok else "RED", desc))
            if not ok:
                red.append(cid)
        red_total += len(red)
        print("  side=%s red=%s" % (side, ",".join(red) if red else "none"))

    print("\nU32-PROBE sides=2 checks_per_side=9 red=%d" % red_total)
    print("RC=%d" % (1 if red_total else 0))
    return 1 if red_total else 0


if __name__ == "__main__":
    sys.exit(main())
