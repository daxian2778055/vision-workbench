// 验证替换版下 HALCON 像素数据访问（OpenCV 桥接前提）：
// GetImagePointer1/GetImagePointer3/GetRegionPoints 等
#include <halconcpp/HalconCpp.h>
#include <cstdio>
#include <cstring>

using namespace HalconCpp;

int main()
{
    // 1) 单通道图像像素指针
    try {
        HImage img;
        GenImageConst(&img, "byte", 64, 64);
        HTuple w, h;
        GetImageSize(img, &w, &h);
        HTuple pointer, type;
        GetImagePointer1(img, &pointer, &type, &w, &h);
        const unsigned char *ptr = reinterpret_cast<const unsigned char *>(pointer.L());
        std::printf("[P1] GetImagePointer1: ptr=%p w=%d h=%d\n", (const void *)ptr, w.I(), h.I());
        if (ptr) {
            unsigned char v = ptr[0];
            std::printf("[P1] 首像素值=%u %s\n", v, v == 0 ? "(OK)" : "(WRONG)");
        }
    } catch (const HException &e) {
        std::printf("[P1] EXC: %s\n", e.ErrorMessage().TextA());
    }

    // 2) 写像素后读回（验证像素可写）
    try {
        HImage img;
        GenImageConst(&img, "byte", 64, 64);
        HTuple pointer, type, w, h;
        GetImagePointer1(img, &pointer, &type, &w, &h);
        unsigned char *ptr = reinterpret_cast<unsigned char *>(pointer.L());
        // 写入首行 255
        for (int c = 0; c < 10; ++c) ptr[c] = 255;
        // 用 GetGrayval 读回验证
        HTuple g;
        GetGrayval(img, 0, 5, &g);
        std::printf("[P2] 写入后 GetGrayval(0,5)=%d %s\n", g.I(), g.I() == 255 ? "(OK)" : "(WRONG)");
    } catch (const HException &e) {
        std::printf("[P2] EXC: %s\n", e.ErrorMessage().TextA());
    }

    // 3) 三通道像素指针
    try {
        HImage img;
        GenImageConst(&img, "byte", 64, 64);
        // 构造 3 通道：Compose3
        HObject rgb;
        Compose3(img, img, img, &rgb);
        HImage rgbImg(rgb);
        HTuple ptr, type, w, h;
        GetImagePointer3(rgbImg, &ptr, &ptr, &ptr, &type, &w, &h);
        std::printf("[P3] GetImagePointer3: w=%d h=%d %s\n", w.I(), h.I(), w.I() == 64 ? "(OK)" : "(WRONG)");
    } catch (const HException &e) {
        std::printf("[P3] EXC: %s\n", e.ErrorMessage().TextA());
    }

    // 4) 区域像素坐标（RegionPoints）——替换版区域异常时此路不通的验证
    try {
        HObject rect;
        GenRectangle1(&rect, 10, 10, 19, 19);
        HTuple r, c;
        GetRegionPoints(rect, &r, &c);
        std::printf("[P4] GetRegionPoints 点数=%d %s\n", r.Length(), r.Length() == 100 ? "(OK)" : "(WRONG)");
    } catch (const HException &e) {
        std::printf("[P4] EXC: %s\n", e.ErrorMessage().TextA());
    }

    // 5) 从像素坐标重建区域（gen_region_points）——OpenCV 掩码→HALCON 区域路径验证
    try {
        HTuple rows, cols;
        for (int i = 0; i < 100; ++i) {
            rows.Append(10 + i / 10);
            cols.Append(10 + i % 10);
        }
        HObject region;
        GenRegionPoints(&region, rows, cols);
        HTuple area;
        AreaCenter(region, &area, &area, &area);
        std::printf("[P5] GenRegionPoints 重建区域面积=%.1f %s\n", area.D(), area.D() == 100.0 ? "(OK)" : "(WRONG)");
    } catch (const HException &e) {
        std::printf("[P5] EXC: %s\n", e.ErrorMessage().TextA());
    }

    return 0;
}
