// 替换版环境下 OCR/条码/测量 实际识别行为测试
#include <halconcpp/HalconCpp.h>
#include <cstdio>

using namespace HalconCpp;

int main()
{
    // ===== 1) OCR 实际识别（DoOcrMulti 依赖输入区域） =====
    std::printf("--- OCR ---\n");
    try {
        HTuple ocrHandle;
        const char *font = "D:/Program Files/MVTec/HALCON-24.11-Progress-Steady/ocr/Industrial_0-9A-Z_NoRej.omc";
        ReadOcrClassMlp(font, &ocrHandle);
        std::printf("OCR 模型加载 OK\n");

        // 合成 200x200 灰度图（背景 0）
        HImage img;
        GenImageConst(&img, "byte", 200, 200);
        HTuple pointer, type, w, h;
        GetImagePointer1(img, &pointer, &type, &w, &h);
        unsigned char *ptr = reinterpret_cast<unsigned char *>(pointer.L());
        // 画三个"字符块"（模拟文字像素）
        for (int y = 50; y < 80; ++y)
            for (int x = 50; x < 70; ++x)
                ptr[y * 200 + x] = 255;
        for (int y = 50; y < 80; ++y)
            for (int x = 90; x < 110; ++x)
                ptr[y * 200 + x] = 255;
        for (int y = 50; y < 80; ++y)
            for (int x = 130; x < 150; ++x)
                ptr[y * 200 + x] = 255;

        // 字符区域：三个矩形（图像→区域：threshold 在替换版损坏，改用 GenRectangle1 构造）
        HObject regions, reg1, reg2, reg3;
        GenRectangle1(&reg1, 50, 50, 79, 69);
        GenRectangle1(&reg2, 50, 90, 79, 109);
        GenRectangle1(&reg3, 50, 130, 79, 149);
        HTuple r1, c1, r2, c2;
        GetRegionPoints(reg1, &r1, &c1);
        std::printf("reg1 点数=%d（应 20x30=600）\n", r1.Length());

        ConcatObj(reg1, reg2, &regions);
        ConcatObj(regions, reg3, &regions);

        HTuple classes, confidences;
        DoOcrMulti(regions, img, ocrHandle, &classes, &confidences);
        std::printf("DoOcrMulti OK: classes=%d 个\n", classes.Length());
        for (int i = 0; i < classes.Length(); ++i) {
            std::printf("  字符[%d]=%s conf=%.2f\n", i, classes[i].S().TextA(),
                        confidences[i].D());
        }
        ClearOcrClassMlp(ocrHandle);
    } catch (const HException &e) {
        std::printf("OCR EXC: %s\n", e.ErrorMessage().TextA());
    }

    // ===== 2) 条码解码（FindBarCode 输出 SymbolRegions 区域） =====
    std::printf("\n--- Barcode ---\n");
    try {
        HTuple barcodeHandle;
        CreateBarCodeModel(HTuple(), HTuple(), &barcodeHandle);
        std::printf("条码模型创建 OK\n");

        HImage img;
        GenImageConst(&img, "byte", 400, 300);
        // 合成"伪条码"条纹（黑白交替竖条）
        HTuple pointer, type, w, h;
        GetImagePointer1(img, &pointer, &type, &w, &h);
        unsigned char *ptr = reinterpret_cast<unsigned char *>(pointer.L());
        for (int x = 0; x < 400; ++x) {
            const unsigned char v = (x / 4) % 2 ? 0 : 255;
            for (int y = 0; y < 300; ++y)
                ptr[y * 400 + x] = v;
        }
        HObject symbolRegions;
        HTuple decoded;
        FindBarCode(img, &symbolRegions, barcodeHandle, "auto", &decoded);
        std::printf("FindBarCode 调用完成（无异常）: decoded=%d 条\n", decoded.Length());
        // 检查符号区域是否有效（替换版区域损坏时此处暴露）
        try {
            HTuple area;
            AreaCenter(symbolRegions, &area, &area, &area);
            std::printf("symbolRegions 面积=%.1f\n", area.D());
        } catch (const HException &e) {
            std::printf("symbolRegions 面积查询 EXC: %s\n", e.ErrorMessage().TextA());
        }
        ClearBarCodeModel(barcodeHandle);
    } catch (const HException &e) {
        std::printf("Barcode EXC: %s\n", e.ErrorMessage().TextA());
    }

    // ===== 3) HALCON 测量（metrology）在替换版下复测 =====
    std::printf("\n--- Metrology 复测 ---\n");
    try {
        HImage img;
        GenImageConst(&img, "byte", 400, 400);
        HTuple pointer, type, w, h;
        GetImagePointer1(img, &pointer, &type, &w, &h);
        unsigned char *ptr = reinterpret_cast<unsigned char *>(pointer.L());
        for (int y = 150; y < 250; ++y)
            for (int x = 100; x < 300; ++x)
                ptr[y * 400 + x] = 200;
        HTuple handle;
        CreateMetrologyModel(&handle);
        HTuple index;
        AddMetrologyObjectLineMeasure(handle, 200, 50, 200, 350, 30, 10, 1.0, 30.0,
                                      "measure_transition", "all", &index);
        ApplyMetrologyModel(img, handle);
        HTuple score;
        GetMetrologyObjectResult(handle, index, "all", "score", HTuple(), &score);
        std::printf("metrology score len=%d %s\n", score.Length(),
                    score.Length() > 0 ? "(可用)" : "(不可用：无边缘)");
        ClearMetrologyModel(handle);
    } catch (const HException &e) {
        std::printf("Metrology EXC: %s\n", e.ErrorMessage().TextA());
    }

    return 0;
}
