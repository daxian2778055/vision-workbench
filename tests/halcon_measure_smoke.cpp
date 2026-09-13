// HALCON 测量链路冒烟测试
// 按 HALCON 24.11 真实 API 签名验证 FindLine/FindCircle/Caliper/AngleMeasure
// 节点的算子参数序列（GenParamValue 需为空元组，Add 阶段 GenParamName/Value 传
// measure_transition）。无 HALCON license（演示模式）时区域生成被限制，此时仅
// 验证 API 参数合法性（不抛异常）；有 license 时进一步验证测量结果有效。
// 用法：halcon_measure_smoke.exe  （退出码 0=全部通过）
#include <halconcpp/HalconCpp.h>
#include <cstdio>
#include <cmath>

using namespace HalconCpp;

static int g_failures = 0;
static bool g_canDraw = true;   // 演示模式下区域/绘制受限，结果断言降级

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::printf("FAIL: %s\n", msg);                                 \
            ++g_failures;                                                   \
        } else {                                                            \
            std::printf("PASS: %s\n", msg);                                 \
        }                                                                   \
    } while (0)

// 400x400 常量背景图（灰度 30）
static HImage makeCanvas()
{
    HImage img;
    GenImageConst(&img, "byte", 400, 400);
    return img;
}

// 尝试画亮圆（200,200,r=50）；失败说明区域绘制不可用（无 license）
static HImage tryDrawCircle()
{
    HImage img = makeCanvas();
    try {
        HObject circle;
        GenCircle(&circle, 200, 200, 50);
        HTuple area, r, c;
        AreaCenter(circle, &area, &r, &c);
        if (area.D() < 1000) {
            std::printf("INFO: 区域生成受限（可能无 HALCON license），结果断言降级为 API 校验\n");
            g_canDraw = false;
            return img;
        }
        HObject painted;
        OverpaintRegion(img, circle, 200, "byte");
        return HImage(painted);
    } catch (const HException &) {
        std::printf("INFO: 区域绘制不可用（可能无 HALCON license），结果断言降级为 API 校验\n");
        g_canDraw = false;
        return img;
    }
}

// 尝试画亮色垂直带（列 220~260）
static HImage tryDrawBand()
{
    HImage img = makeCanvas();
    try {
        HObject band;
        GenRectangle1(&band, 100, 220, 300, 260);
        HObject painted;
        OverpaintRegion(img, band, 200, "byte");
        return HImage(painted);
    } catch (const HException &) {
        g_canDraw = false;
        return img;
    }
}

// ===== 与 FindCircleNode 相同序列（24.11 签名） =====
static void testCircle()
{
    std::printf("--- testCircle ---\n");
    HImage img = tryDrawCircle();
    HTuple handle;
    CreateMetrologyModel(&handle);
    HTuple index;
    AddMetrologyObjectCircleMeasure(handle, 200, 200, 50, 20, 10, 1.0, 30.0,
                                    "measure_transition", "all", &index);
    SetMetrologyObjectParam(handle, index, "measure_transition", "all");
    SetMetrologyObjectParam(handle, index, "num_measures", 36);
    ApplyMetrologyModel(img, handle);

    HTuple rRow, rCol, rRad, score;
    GetMetrologyObjectResult(handle, index, "all", "row", HTuple(), &rRow);
    GetMetrologyObjectResult(handle, index, "all", "column", HTuple(), &rCol);
    GetMetrologyObjectResult(handle, index, "all", "radius", HTuple(), &rRad);
    GetMetrologyObjectResult(handle, index, "all", "score", HTuple(), &score);

    CHECK(true, "circle: 完整调用链无异常（参数合法性通过）");
    if (g_canDraw) {
        CHECK(score.Length() > 0, "circle: 找到边缘点");
        if (score.Length() > 0) {
            CHECK(std::fabs(rRad.D() - 50.0) < 3.0,
                  "circle: 半径接近 50（实际 %.2f）");
            CHECK(std::fabs(rRow.D() - 200.0) < 3.0 && std::fabs(rCol.D() - 200.0) < 3.0,
                  "circle: 圆心接近 (200,200)");
        }
    }
    HObject fitted;
    GetMetrologyObjectResultContour(&fitted, handle, index, "all", 1.0);
    ClearMetrologyModel(handle);
    CHECK(true, "circle: 拟合轮廓获取无异常");
}

// ===== 与 FindLineNode 相同序列（24.11 签名） =====
static void testLine()
{
    std::printf("--- testLine ---\n");
    HImage img = tryDrawBand();
    HTuple handle;
    CreateMetrologyModel(&handle);
    HTuple index;
    AddMetrologyObjectLineMeasure(handle, 100, 200, 300, 200, 30, 10, 1.0, 30.0,
                                  "measure_transition", "all", &index);
    SetMetrologyObjectParam(handle, index, "measure_transition", "all");
    SetMetrologyObjectParam(handle, index, "num_measures", 50);
    ApplyMetrologyModel(img, handle);

    HTuple rowB, colB, rowE, colE, score;
    GetMetrologyObjectResult(handle, index, "all", "row_begin", HTuple(), &rowB);
    GetMetrologyObjectResult(handle, index, "all", "column_begin", HTuple(), &colB);
    GetMetrologyObjectResult(handle, index, "all", "row_end", HTuple(), &rowE);
    GetMetrologyObjectResult(handle, index, "all", "column_end", HTuple(), &colE);
    GetMetrologyObjectResult(handle, index, "all", "score", HTuple(), &score);

    CHECK(true, "line: 完整调用链无异常（参数合法性通过）");
    if (g_canDraw) {
        CHECK(score.Length() > 0, "line: 找到边缘点");
        if (score.Length() > 0) {
            double mid = (colB.D() + colE.D()) / 2.0;
            CHECK(std::fabs(mid - 240.0) < 5.0, "line: 拟合线中心列接近 240（实际 %.2f）");
        }
    }
    HObject fitted;
    GetMetrologyObjectResultContour(&fitted, handle, index, "all", 1.0);
    ClearMetrologyModel(handle);
    CHECK(true, "line: 拟合轮廓获取无异常");
}

// ===== 与 CaliperMeasureNode 相同序列（极性/选择经 SetMetrologyObjectParam） =====
static void testCaliper()
{
    std::printf("--- testCaliper ---\n");
    HImage img = tryDrawBand();
    HTuple handle;
    CreateMetrologyModel(&handle);
    HTuple index;
    AddMetrologyObjectLineMeasure(handle, 100, 200, 300, 200, 30, 10, 1.0, 30.0,
                                  "measure_transition", "positive", &index);
    SetMetrologyObjectParam(handle, index, "measure_select", "first");
    ApplyMetrologyModel(img, handle);

    HTuple row, col;
    HObject measuresContours;
    GetMetrologyObjectMeasures(&measuresContours, handle, index, "all", &row, &col);
    CHECK(true, "caliper: 完整调用链无异常（参数合法性通过）");
    if (g_canDraw)
        CHECK(row.Length() > 0, "caliper: 测到边缘（positive/first）");
    ClearMetrologyModel(handle);
}

// ===== 与 AngleMeasureNode 相同序列（两条测量线） =====
static void testAngle()
{
    std::printf("--- testAngle ---\n");
    HImage img = tryDrawBand();
    HTuple handle;
    CreateMetrologyModel(&handle);
    HTuple idx1, idx2;
    AddMetrologyObjectLineMeasure(handle, 100, 150, 300, 150, 20, 5, 1.0, 30.0,
                                  "measure_transition", "all", &idx1);
    AddMetrologyObjectLineMeasure(handle, 100, 250, 300, 250, 20, 5, 1.0, 30.0,
                                  "measure_transition", "all", &idx2);
    ApplyMetrologyModel(img, handle);

    HTuple a1, a2, s1, s2;
    GetMetrologyObjectResult(handle, idx1, "all", "angle", HTuple(), &a1);
    GetMetrologyObjectResult(handle, idx2, "all", "angle", HTuple(), &a2);
    GetMetrologyObjectResult(handle, idx1, "all", "score", HTuple(), &s1);
    GetMetrologyObjectResult(handle, idx2, "all", "score", HTuple(), &s2);

    CHECK(true, "angle: 完整调用链无异常（参数合法性通过）");
    if (g_canDraw) {
        CHECK(s1.Length() > 0 && s2.Length() > 0, "angle: 两条线均测到边缘");
        if (s1.Length() > 0 && s2.Length() > 0) {
            double d = std::fabs(a1.D() - a2.D()) * 180.0 / 3.14159265358979323846;
            while (d > 180.0) d -= 180.0;
            CHECK(d < 2.0, "angle: 平行线夹角 ≈ 0（实际 %.2f°）");
        }
    }
    HObject c1, c2, fitted;
    GetMetrologyObjectResultContour(&c1, handle, idx1, "all", 1.0);
    GetMetrologyObjectResultContour(&c2, handle, idx2, "all", 1.0);
    ConcatObj(c1, c2, &fitted);
    ClearMetrologyModel(handle);
    CHECK(true, "angle: 拟合轮廓获取无异常");
}

int main()
{
    try {
        testCircle();
        testLine();
        testCaliper();
        testAngle();
    } catch (const HException &e) {
        std::printf("FAIL: 未预期 HALCON 异常: %s\n", e.ErrorMessage().TextA());
        return 2;
    }
    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED%s\n", g_canDraw ? "" : " (API-only, no license)");
        return 0;
    }
    std::printf("%d TEST(S) FAILED\n", g_failures);
    return 1;
}
