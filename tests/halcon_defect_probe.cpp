// 替换版状态依赖实验：不同"预热"序列对区域算子结果的影响
// 目标：找到能使区域功能稳定的调用序列（若存在则可代码规避）
#include <halconcpp/HalconCpp.h>
#include <cstdio>

using namespace HalconCpp;

static void rectArea(const char *tag)
{
    try {
        HObject rect;
        GenRectangle1(&rect, 50, 50, 149, 149);   // 100x100 → 期望 10000
        HTuple area, rr, cc;
        AreaCenter(rect, &area, &rr, &cc);
        std::printf("  [%s] rect(50,50,149,149) area=%.1f %s\n",
                    tag, area.D(),
                    (std::fabs(area.D() - 10000.0) < 1.0) ? "(OK)" : "(WRONG)");
    } catch (const HException &e) {
        std::printf("  [%s] EXC: %s\n", tag, e.ErrorMessage().TextA());
    }
}

static void thresholdArea(const char *tag, int size)
{
    try {
        HImage img;
        GenImageConst(&img, "byte", size, size);
        HObject reg;
        Threshold(img, &reg, 0, 0);
        HTuple area;
        AreaCenter(reg, &area, &area, &area);
        const bool ok = std::fabs(area.D() - double(size * size)) < 1.0;
        std::printf("  [%s] threshold %dx%d area=%.1f %s\n",
                    tag, size, size, area.D(), ok ? "(OK)" : "(WRONG)");
    } catch (const HException &e) {
        std::printf("  [%s] EXC: %s\n", tag, e.ErrorMessage().TextA());
    }
}

int main()
{
    std::printf("=== P1: 裸跑（无预热） ===\n");
    rectArea("P1a");
    thresholdArea("P1b", 64);

    std::printf("=== P2: 图像生成预热 ===\n");
    {
        HImage img;
        GenImageConst(&img, "byte", 400, 400);
    }
    rectArea("P2a");
    thresholdArea("P2b", 64);

    std::printf("=== P3: GetSystem 预热 ===\n");
    {
        HTuple v;
        GetSystem("version", &v);
        GetSystem("license", &v);
    }
    rectArea("P3a");
    thresholdArea("P3b", 64);

    std::printf("=== P4: 图像读取预热（ReadImage） ===\n");
    {
        HImage img;
        ReadImage(&img, "C:/Windows/Temp/vfp_probe_test.png");
    }
    rectArea("P4a");
    thresholdArea("P4b", 64);

    std::printf("=== P5: 小矩形预热 ===\n");
    {
        HObject smallRect;
        GenRectangle1(&smallRect, 0, 0, 9, 9);
        HTuple a;
        AreaCenter(smallRect, &a, &a, &a);
    }
    rectArea("P5a");
    thresholdArea("P5b", 64);

    std::printf("=== P6: 阈值预热（先图像→区域） ===\n");
    {
        HImage img;
        GenImageConst(&img, "byte", 16, 16);
        HObject reg;
        Threshold(img, &reg, 0, 0);
        HTuple a;
        AreaCenter(reg, &a, &a, &a);
    }
    rectArea("P6a");
    thresholdArea("P6b", 64);

    std::printf("=== P7: RegionFeatures 预热（完整 dumpRect 序列） ===\n");
    {
        HObject r;
        GenRectangle1(&r, 10, 10, 19, 19);
        HTuple a, w, h;
        AreaCenter(r, &a, &a, &a);
        RegionFeatures(r, "width", &w);
        RegionFeatures(r, "height", &h);
    }
    rectArea("P7a");
    thresholdArea("P7b", 64);

    std::printf("=== P8: 完整初始化序列后阈值尺寸矩阵 ===\n");
    {
        // 完整预热：矩形 + 面积 + 特征 + 阈值
        HObject r;
        GenRectangle1(&r, 10, 10, 19, 19);
        HTuple a, w;
        AreaCenter(r, &a, &a, &a);
        RegionFeatures(r, "width", &w);
        HImage img;
        GenImageConst(&img, "byte", 16, 16);
        HObject reg;
        Threshold(img, &reg, 0, 0);
        AreaCenter(reg, &a, &a, &a);
    }
    thresholdArea("P8-16", 16);
    thresholdArea("P8-64", 64);
    thresholdArea("P8-256", 256);

    std::printf("=== P9: 完整预热后形态学与测量 ===\n");
    try {
        HObject rect;
        GenRectangle1(&rect, 50, 50, 149, 149);
        HObject dil;
        DilationCircle(rect, &dil, 3.5);
        HTuple a1, a2;
        AreaCenter(rect, &a1, &a1, &a1);
        AreaCenter(dil, &a2, &a2, &a2);
        std::printf("  [P9] dilate 100x100: %.1f -> %.1f %s\n",
                    a1.D(), a2.D(), a2.D() > a1.D() ? "(OK)" : "(WRONG)");
    } catch (const HException &e) {
        std::printf("  [P9] EXC: %s\n", e.ErrorMessage().TextA());
    }
    try {
        HImage img;
        GenImageConst(&img, "byte", 400, 400);
        HTuple handle;
        CreateMetrologyModel(&handle);
        HTuple index;
        AddMetrologyObjectCircleMeasure(handle, 200, 200, 50, 20, 10, 1.0, 30.0,
                                        "measure_transition", "all", &index);
        ApplyMetrologyModel(img, handle);
        HTuple score;
        GetMetrologyObjectResult(handle, index, "all", "score", HTuple(), &score);
        std::printf("  [P9] metrology score len=%d %s\n",
                    score.Length(), score.Length() > 0 ? "(OK)" : "(WRONG)");
        ClearMetrologyModel(handle);
    } catch (const HException &e) {
        std::printf("  [P9] EXC: %s\n", e.ErrorMessage().TextA());
    }
    try {
        // 绘制（OverpaintRegion）
        HImage img;
        GenImageConst(&img, "byte", 400, 400);
        HObject circle;
        GenCircle(&circle, 200, 200, 50);
        HObject painted;
        OverpaintRegion(img, circle, 200, "byte");
        HTuple g;
        GetGrayval(HImage(painted), 200, 200, &g);
        std::printf("  [P9] overpaint gray=%d %s\n",
                    g.I(), g.I() == 200 ? "(OK)" : "(WRONG)");
    } catch (const HException &e) {
        std::printf("  [P9] EXC: %s\n", e.ErrorMessage().TextA());
    }

    return 0;
}