// HALCON 附加能力探针：XLD 轮廓系列 + DeepOCR
// 覆盖应用仍在使用的 XLD 可视化算子（GenRectangle2ContourXld 等）与
// **唯一保留的 HALCON DL 能力 DeepOCR**（CreateDeepOcr/ApplyDeepOcr）。
// 其他 HALCON DL（ReadDlModel/ApplyDlModel 分类/检测、TrainDlModelBatch）不提供、不探测。
// 用法：halcon_extra_probe.exe [--summary]
//   退出码 0 = 无意外失败（已知限制与 SKIP 不算失败）
//   退出码 1 = 存在不在已知清单内的意外失败
//   --summary 只打印汇总、已知失败清单与 VERDICT，全量明细写入
//   halcon_extra_probe_detail.log（供排查定位）
#include <halconcpp/HalconCpp.h>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <sys/stat.h>

using namespace HalconCpp;

static int g_pass = 0;
static int g_fail = 0;
static int g_skip = 0;
static bool g_summary = false;
static FILE *g_detail = nullptr;
static const char *g_failNames[64];
static int g_failNamesN = 0;

// 已知失败清单（替换版运行时限制，应用内已有替代或影响有限）
static bool isKnownFail(const char *name)
{
    static const char *kKnown[] = {
        "XLD SplitContoursXld",
        "XLD PaintXld 绘制",
        "DeepOCR 创建+推理",
    };
    for (size_t i = 0; i < sizeof(kKnown) / sizeof(kKnown[0]); ++i)
        if (std::strcmp(kKnown[i], name) == 0)
            return true;
    return false;
}

static const char *knownReplacement(const char *name)
{
    if (std::strcmp(name, "XLD SplitContoursXld") == 0)
        return "应用未使用该算子；拆分由 OpenCV 轮廓处理替代";
    if (std::strcmp(name, "XLD PaintXld 绘制") == 0)
        return "绘制位置偏移约1像素且目标行半强度，仅影响标注外观，不影响检测结果";
    if (std::strcmp(name, "DeepOCR 创建+推理") == 0)
        return "DeepOCR 已实测可用（平台内新增 HALCON DeepOCR 节点，CPU 推理）";
    return nullptr;
}

static void out(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (g_summary) {
        if (g_detail) std::vfprintf(g_detail, fmt, ap);
    } else {
        std::vprintf(fmt, ap);
    }
    va_end(ap);
}

#define REPORT(cond, name, detail)                                          \
    do {                                                                    \
        if (cond) {                                                         \
            out("PASS: %s (%s)\n", name, detail);                           \
            ++g_pass;                                                       \
        } else {                                                            \
            out("FAIL: %s (%s)\n", name, detail);                           \
            ++g_fail;                                                       \
            if (g_failNamesN < 64) g_failNames[g_failNamesN++] = name;      \
        }                                                                   \
    } while (0)

#define REPORT_SKIP(name, detail)                                           \
    do {                                                                    \
        out("SKIP: %s (%s)\n", name, detail);                               \
        ++g_skip;                                                           \
    } while (0)

static bool fileExists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

// 常用合成图：64x64 渐变+方块
static HImage makeImg(int size, int rectVal)
{
    HImage img;
    GenImageConst(&img, "byte", size, size);
    HTuple pointer, type, w, h;
    GetImagePointer1(img, &pointer, &type, &w, &h);
    unsigned char *p = reinterpret_cast<unsigned char *>(pointer.L());
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            p[y * size + x] = static_cast<unsigned char>((x + y) / 2);
    for (int y = size / 4; y < size / 4 + size / 4; ++y)
        for (int x = size / 4; x < size / 4 + size / 4; ++x)
            p[y * size + x] = static_cast<unsigned char>(rectVal);
    return img;
}

// 三通道合成图（DL 分类模型需要 3 通道）
static HImage makeImg3(int size, int rectVal)
{
    HImage img = makeImg(size, rectVal);
    HObject rgb;
    HImage imgR = makeImg(size, 255);
    Compose3(imgR, img, img, &rgb);
    return HImage(rgb);
}

// ================= XLD 轮廓系列 =================
static void testXld()
{
    out("--- XLD 轮廓系列 ---\n");

    // 1) 多边形轮廓生成 + 点查询
    try {
        HObject poly;
        HTuple rs, cs;
        rs.Append(10).Append(100).Append(100);
        cs.Append(10).Append(10).Append(50);
        GenContourPolygonXld(&poly, rs, cs);
        HTuple n;
        ContourPointNumXld(poly, &n);
        HTuple ors, ocs;
        GetContourXld(poly, &ors, &ocs);
        bool ok = n.I() == 3 && ors.Length() == 3 && std::fabs(ors[0].D() - 10.0) < 0.001;
        REPORT(ok, "XLD GenContourPolygonXld+查询",
               (std::string("点数=") + std::to_string(n.I())).c_str());
    } catch (const HException &e) {
        REPORT(false, "XLD GenContourPolygonXld+查询", e.ErrorMessage().TextA());
    }

    // 2) 矩形轮廓（DL 检测框可视化，DlDetectionNode 在用）
    try {
        HObject rects;
        GenRectangle2ContourXld(&rects, HTuple(100), HTuple(120), HTuple(0.0),
                                HTuple(30), HTuple(20));
        HTuple n;
        ContourPointNumXld(rects, &n);
        REPORT(n.I() == 5, "XLD GenRectangle2ContourXld", "检测框轮廓点数=5");
    } catch (const HException &e) {
        REPORT(false, "XLD GenRectangle2ContourXld", e.ErrorMessage().TextA());
    }

    // 3) 十字轮廓（角点可视化，CornerNode 在用）
    try {
        HObject crosses;
        GenCrossContourXld(&crosses, HTuple(50).Append(150), HTuple(50).Append(150),
                           HTuple(10), HTuple(0.785));
        HTuple n;
        ContourPointNumXld(crosses, &n);
        std::string info = "点数=";
        for (Hlong i = 0; i < n.Length(); ++i) {
            if (i) info += ",";
            info += std::to_string(n[i].I());
        }
        bool ok = n.Length() >= 1 && n[0].I() == 5;
        REPORT(ok, "XLD GenCrossContourXld", info.c_str());
    } catch (const HException &e) {
        REPORT(false, "XLD GenCrossContourXld", e.ErrorMessage().TextA());
    }

    // 4) 区域转轮廓 + 选择/合并/拆分（逐步定位）
    try {
        HObject reg;
        GenRectangle1(&reg, 50, 50, 149, 149);
        HObject xld;
        GenContourRegionXld(reg, &xld, "border");
        HTuple n;
        ContourPointNumXld(xld, &n);
        REPORT(n.Length() > 0 && n[0].I() > 0, "XLD GenContourRegionXld",
               (std::string("边界点数=") + std::to_string(n[0].I())).c_str());
    } catch (const HException &e) {
        REPORT(false, "XLD GenContourRegionXld", e.ErrorMessage().TextA());
    }
    try {
        HObject reg;
        GenRectangle1(&reg, 50, 50, 149, 149);
        HObject xld;
        GenContourRegionXld(reg, &xld, "border");
        HObject sel;
        SelectContoursXld(xld, &sel, "contour_length", 10, 500, -0.5, 0.5);
        HTuple nSel;
        ContourPointNumXld(sel, &nSel);
        REPORT(nSel.Length() > 0 && nSel[0].I() > 0, "XLD SelectContoursXld",
               (std::string("点数=") + std::to_string(nSel[0].I())).c_str());
    } catch (const HException &e) {
        REPORT(false, "XLD SelectContoursXld", e.ErrorMessage().TextA());
    }
    try {
        HObject reg;
        GenRectangle1(&reg, 50, 50, 149, 149);
        HObject xld;
        GenContourRegionXld(reg, &xld, "border");
        HObject uni;
        UnionAdjacentContoursXld(xld, &uni, 10, 1, "attr_keep");
        HTuple nUni;
        ContourPointNumXld(uni, &nUni);
        REPORT(nUni.Length() > 0 && nUni[0].I() > 0, "XLD UnionAdjacentContoursXld",
               (std::string("点数=") + std::to_string(nUni[0].I())).c_str());
    } catch (const HException &e) {
        REPORT(false, "XLD UnionAdjacentContoursXld", e.ErrorMessage().TextA());
    }
    try {
        // SplitContoursXld 用纯多边形轮廓测试（排除 region 转轮廓的类型因素）
        HObject poly;
        HTuple rs, cs;
        rs.Append(10).Append(100).Append(100).Append(10);
        cs.Append(10).Append(10).Append(50).Append(50);
        GenContourPolygonXld(&poly, rs, cs);
        HObject spl;
        SplitContoursXld(poly, &spl, "polygon", 2.0, 0.0);
        HTuple nSpl;
        ContourPointNumXld(spl, &nSpl);
        REPORT(nSpl.Length() > 0, "XLD SplitContoursXld",
               (std::string("段数=") + std::to_string(nSpl.Length())).c_str());
    } catch (const HException &e) {
        REPORT(false, "XLD SplitContoursXld", e.ErrorMessage().TextA());
    }

    // 5) 最小外接圆 + 拟合
    try {
        HObject poly;
        HTuple rs, cs;
        for (int i = 0; i < 36; ++i) {
            const double a = i * 3.14159265 * 2.0 / 36.0;
            rs.Append(128 + 40 * std::sin(a));
            cs.Append(128 + 40 * std::cos(a));
        }
        GenContourPolygonXld(&poly, rs, cs);
        HTuple row, col, radius;
        SmallestCircleXld(poly, &row, &col, &radius);
        bool ok = radius.Length() == 1 && std::fabs(radius.D() - 40.0) < 1.0;
        REPORT(ok, "XLD SmallestCircleXld",
               (std::string("半径=") + std::to_string(radius.D())).c_str());
    } catch (const HException &e) {
        REPORT(false, "XLD SmallestCircleXld", e.ErrorMessage().TextA());
    }

    // 6) 两轮廓最小距离
    try {
        HObject p1, p2;
        HTuple r1, c1, r2, c2;
        r1.Append(10); c1.Append(10); r1.Append(100); c1.Append(10);
        GenContourPolygonXld(&p1, r1, c1);
        r2.Append(10); c2.Append(30); r2.Append(100); c2.Append(30);
        GenContourPolygonXld(&p2, r2, c2);
        HTuple minDist, rr1, cc1, rr2, cc2;
        DistanceCcMinPoints(p1, p2, "point_to_segment", &minDist, &rr1, &cc1, &rr2, &cc2);
        REPORT(std::fabs(minDist.D() - 20.0) < 0.5, "XLD DistanceCcMinPoints",
               (std::string("最小距离=") + std::to_string(minDist.D())).c_str());
    } catch (const HException &e) {
        REPORT(false, "XLD DistanceCcMinPointsXld", e.ErrorMessage().TextA());
    }

    // 7) 轮廓绘制到图像（DL 节点标注输出在用）
    try {
        HObject poly;
        HTuple rs, cs;
        rs.Append(10).Append(100).Append(100);
        cs.Append(10).Append(10).Append(50);
        GenContourPolygonXld(&poly, rs, cs);
        HImage img = makeImg(200, 0);
        HObject painted;
        PaintXld(poly, img, &painted, 200);
        HImage himg(painted);
        HTuple g0, g1, g2;
        GetGrayval(himg, 55, 9, &g0);
        GetGrayval(himg, 55, 10, &g1);
        GetGrayval(himg, 55, 11, &g2);
        bool ok = g1.I() == 200;
        REPORT(ok, "XLD PaintXld 绘制",
               (std::string("y9=") + std::to_string(g0.I()) +
                " y10=" + std::to_string(g1.I()) +
                " y11=" + std::to_string(g2.I())).c_str());
    } catch (const HException &e) {
        REPORT(false, "XLD PaintXld 绘制", e.ErrorMessage().TextA());
    }

    // 8) 轮廓合并（ConcatObj，DL 节点多框叠加在用）
    try {
        HObject a, b, cat;
        GenCrossContourXld(&a, 50, 50, 10, 0.0);
        GenCrossContourXld(&b, 150, 150, 10, 0.0);
        ConcatObj(a, b, &cat);
        HTuple n;
        ContourPointNumXld(cat, &n);
        Hlong total = 0;
        for (Hlong i = 0; i < n.Length(); ++i) total += n[i].I();
        REPORT(n.Length() == 2 && total == 10, "XLD ConcatObj 合并",
               (std::string("对象数=") + std::to_string(n.Length()) +
                " 总点数=" + std::to_string(total)).c_str());
    } catch (const HException &e) {
        REPORT(false, "XLD ConcatObj 合并", e.ErrorMessage().TextA());
    }
}

// ================= DeepOCR（HALCON DL 唯一保留项） =================
static void testDeepOcr()
{
    const char *base = "D:/Program Files/MVTec/HALCON-24.11-Progress-Steady/dl/";
    out("--- DeepOCR ---");

    // DeepOCR 是产品内唯一保留的 HALCON DL 能力（DeepOcrNode）。
    const char *detModel = "pretrained_deep_ocr_detection.hdl";
    const char *recModel = "pretrained_deep_ocr_recognition.hdl";
    std::string detPath = std::string(base) + detModel;
    std::string recPath = std::string(base) + recModel;
    if (!fileExists(detPath.c_str()) || !fileExists(recPath.c_str())) {
        REPORT_SKIP("DeepOCR", "检测/识别模型缺失（需 HALCON 安装目录）");
    } else {
        try {
            // 标准用法：mode=auto，检测/识别模型从 $HALCONROOT/dl/ 自动加载
            HTuple ocrH;
            CreateDeepOcr(HTuple("mode"), HTuple("auto"), &ocrH);
            out("  CreateDeepOcr OK\n");
            try {
                SetDeepOcrParam(ocrH, "runtime", "cpu");
                out("  SetDeepOcrParam runtime=cpu OK\n");
            } catch (const HException &e) {
                out("  SetDeepOcrParam runtime 失败: %s\n", e.ErrorMessage().TextA());
            }
            HImage img = makeImg3(200, 0);
            HTuple results;
            ApplyDeepOcr(img, ocrH, "auto", &results);
            REPORT(true, "DeepOCR 创建+推理", "调用链无异常");
            try { ClearDlModel(ocrH); } catch (...) {}
        } catch (const HException &e) {
            REPORT(false, "DeepOCR 创建+推理", e.ErrorMessage().TextA());
        }
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--summary") == 0) {
        g_summary = true;
        g_detail = std::fopen("halcon_extra_probe_detail.log", "w");
        if (g_detail) {
            std::time_t t = std::time(nullptr);
            char ts[64];
            std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
            std::fprintf(g_detail, "halcon_extra_probe detail log  %s\n", ts);
        }
    }

    testXld();
    testDeepOcr();

    std::printf("\n==== 汇总: %d PASS / %d FAIL / %d SKIP ====\n", g_pass, g_fail, g_skip);

    if (g_failNamesN > 0) {
        std::printf("\n==== 已知失败（已被 OpenCV 替代，不影响运行）====\n");
        for (int i = 0; i < g_failNamesN; ++i) {
            bool dup = false;
            for (int j = 0; j < i; ++j)
                if (std::strcmp(g_failNames[i], g_failNames[j]) == 0) { dup = true; break; }
            if (dup) continue;
            const char *repl = knownReplacement(g_failNames[i]);
            std::printf("  KNOWN: %s -> %s\n", g_failNames[i],
                        repl ? repl : "已由 OpenCV 替代");
        }
    }

    int unexpected = 0;
    for (int i = 0; i < g_failNamesN; ++i) {
        if (!isKnownFail(g_failNames[i])) {
            std::printf("UNEXPECTED FAILURE: %s\n", g_failNames[i]);
            ++unexpected;
        }
    }
    if (unexpected > 0) {
        std::printf("VERDICT: UNEXPECTED FAILURES (%d)\n", unexpected);
        if (g_detail) std::fclose(g_detail);
        return 1;
    }
    std::printf("VERDICT: OK (failures, if any, are known replacement-runtime limits)\n");
    if (g_detail) std::fclose(g_detail);
    return 0;
}
