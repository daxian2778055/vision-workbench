// DL OCR（DeepOcr）在替换版下可用性测试——不依赖区域算子的 OCR 替代路径
#include <halconcpp/HalconCpp.h>
#include <cstdio>

using namespace HalconCpp;

int main()
{
    try {
        const char *dir = "D:/Program Files/MVTec/HALCON-24.11-Progress-Steady/dl/";
        HTuple detHandle, recHandle;
        ReadDlModel((std::string(dir) + "pretrained_deep_ocr_detection.hdl").c_str(), &detHandle);
        std::printf("DeepOcr 检测模型加载 OK\n");
        ReadDlModel((std::string(dir) + "pretrained_deep_ocr_recognition.hdl").c_str(), &recHandle);
        std::printf("DeepOcr 识别模型加载 OK\n");

        // 设备设 CPU（替换版 GPU 路径未知，CPU 最稳）
        SetDlModelParam(detHandle, "device", "cpu");
        SetDlModelParam(recHandle, "device", "cpu");

        HTuple ocrHandle;
        HTuple gpn, gpv;
        gpn.Append("detection_model");
        gpv.Append(detHandle);
        gpn.Append("recognition_model");
        gpv.Append(recHandle);
        CreateDeepOcr(gpn, gpv, &ocrHandle);
        std::printf("CreateDeepOcr OK\n");

        HImage img;
        GenImageConst(&img, "byte", 200, 80);
        HTuple pointer, type, w, h;
        GetImagePointer1(img, &pointer, &type, &w, &h);
        unsigned char *ptr = reinterpret_cast<unsigned char *>(pointer.L());
        // 简单模拟文字块
        for (int y = 20; y < 60; ++y)
            for (int x = 20; x < 40; ++x)
                ptr[y * 200 + x] = 255;
        for (int y = 20; y < 60; ++y)
            for (int x = 60; x < 80; ++x)
                ptr[y * 200 + x] = 255;

        HTuple results;
        ApplyDeepOcr(img, ocrHandle, "inference", &results);
        std::printf("ApplyDeepOcr 调用完成（无异常），结果元组长度=%d\n", results.Length());

        ClearDlModel(ocrHandle);
        ClearDlModel(detHandle);
        ClearDlModel(recHandle);
        std::printf("DL OCR 链路可用\n");
    } catch (const HException &e) {
        std::printf("DL OCR EXC: %s\n", e.ErrorMessage().TextA());
    }
    return 0;
}
