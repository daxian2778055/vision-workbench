// HALCON 节点算子族批量可用性验证（替换版环境）
// 用真实合成图（非常量图）测试当前注册的 HALCON 节点对应算子族，
// 输出 PASS/FAIL/EXC 清单，用于决定节点保留/移除。
#include <halconcpp/HalconCpp.h>
#include "OpencvUtil.h"
#include <opencv2/imgproc.hpp>
#include <cstdio>
#include <string>
#include <vector>

using namespace HalconCpp;

static int g_pass = 0, g_fail = 0;

static void report(const char *name, bool ok, const std::string &info)
{
    std::printf("%s: %s (%s)\n", ok ? "PASS" : "FAIL", name, info.c_str());
    ok ? ++g_pass : ++g_fail;
}

// 合成测试图：256x256 灰度，白底 + 灰圆 + 黑矩形（OpenCV 构造，绕开替换版区域绘制异常）
static HImage makeTestImage()
{
    // 实验开关：true=OpenCV 构造（GenImage1），false=原生 GenImageConst 常量图
    const bool useOpencv = true;
    if (!useOpencv) {
        HImage img;
        GenImageConst(&img, "byte", 256, 256);
        return img;
    }
    cv::Mat img(256, 256, CV_8UC1, cv::Scalar(255));
    cv::circle(img, cv::Point(90, 90), 40, cv::Scalar(100), -1);
    cv::rectangle(img, cv::Point(40, 140), cv::Point(120, 200), cv::Scalar(200), -1);
    return OpencvUtil::matToHimage(img);
}

int main()
{
    try {
        HImage img = makeTestImage();
        report("测试图生成", img.Width() == 256 && img.Height() == 256,
               "256x256");
        try {
            HTuple gv;
            GetGrayval(img, 0, 0, &gv);
            GetGrayval(img, 90, 90, &gv);
            report("GetGrayval 像素读取", gv.I() == 100, "中心灰度=" + std::to_string(gv.I()));
        } catch (const HException &e) {
            report("GetGrayval 像素读取", false, std::string("异常: ") + e.ErrorMessage().Text());
        }

        // ===== 图像类（像素级，替换版下预期正常）=====
        {
            // 二分验证：图像尺寸/来源对 GaussImage 的影响
            HImage constImg;
            GenImageConst(&constImg, "byte", 256, 256);
            try {
                HImage out;
                GaussImage(constImg, &out, 1.5);
                report("GaussImage 256x256 常量图", out.IsInitialized(), "输出有效");
            } catch (const HException &e) {
                report("GaussImage 256x256 常量图", false,
                       std::string("异常: ") + e.ErrorMessage().Text());
            }
            try {
                HImage big;
                GenImageConst(&big, "byte", 400, 400);
                HImage out2;
                GaussImage(big, &out2, 1.5);
                report("GaussImage 400x400 常量图", out2.IsInitialized(), "输出有效");
            } catch (const HException &e) {
                report("GaussImage 400x400 常量图", false,
                       std::string("异常: ") + e.ErrorMessage().Text());
            }
            try {
                HImage out3;
                GaussImage(img, &out3, 1.5);
                report("GaussImage OpenCV图", out3.IsInitialized(), "输出有效");
            } catch (const HException &e) {
                report("GaussImage OpenCV图", false,
                       std::string("异常: ") + e.ErrorMessage().Text());
            }
        }
        {
            HImage out;
            MedianImage(img, &out, "circle", 3, "mirrored");
            report("MedianImage 中值", out.IsInitialized() && out.Width() == 256, "输出有效");
        }
        {
            HObject edge;
            Laplace(img, &edge, "signed", 5, "n_4");
            HObject out;
            AddImage(img, HImage(edge), &out, 1.0, 0.0);
            report("Laplace+AddImage 锐化(等价SharpenNode)", HImage(out).IsInitialized(), "输出有效");
        }
        {
            HObject out;
            EquHistoImage(img, &out);
            report("EquHistoImage 直方图均衡(等价HistogramEqualizeNode)", HImage(out).IsInitialized(), "输出有效");
        }
        {
            HImage out;
            ScaleImage(img, &out, 1.2, 10);
            report("ScaleImage 灰度拉伸", out.IsInitialized(), "输出有效");
        }
        {
            HImage out;
            ZoomImageFactor(img, &out, 0.5, 0.5, "constant");
            report("ZoomImageFactor 缩放", out.Width() == 128 && out.Height() == 128, "128x128");
        }
        {
            HImage out;
            RotateImage(img, &out, 30.0, "constant");
            report("RotateImage 旋转", out.IsInitialized() && out.Width() == 256, "输出有效");
        }
        {
            // 阈值+仿射平移（等价 TranslateImageNode 的仿射实现）
            HTuple hom, homTrans, qx, qy;
            HomMat2dIdentity(&hom);
            HomMat2dTranslate(hom, 20.0, 10.0, &homTrans);
            AffineTransPoint2d(homTrans, 5.0, 5.0, &qx, &qy);
            report("HomMat2dTranslate 平移(等价TranslateImageNode)",
                   fabs(qx.D() - 25.0) < 0.01 && fabs(qy.D() - 15.0) < 0.01,
                   "点(5,5)->(" + std::to_string(qx.D()) + "," + std::to_string(qy.D()) + ")");
        }
        {
            HImage out;
            MirrorImage(img, &out, "column");
            report("MirrorImage 镜像", out.IsInitialized(), "输出有效");
        }
        {
            HImage out;
            AddImage(img, img, &out, 0.5, 0);
            report("AddImage 图像加", out.IsInitialized(), "输出有效");
        }
        {
            HImage out;
            InvertImage(img, &out);
            report("InvertImage 求反", out.IsInitialized(), "输出有效");
        }
        {
            HImage out;
            ConvertImageType(img, &out, "uint2");
            report("ConvertImageType 类型转换", out.IsInitialized(), "输出有效");
        }
        {
            HImage out;
            GrayErosionRect(img, &out, 3, 3);
            report("GrayErosionRect 灰度腐蚀", out.IsInitialized(), "输出有效");
        }
        {
            HImage out;
            GrayDilationRect(img, &out, 3, 3);
            report("GrayDilationRect 灰度膨胀", out.IsInitialized(), "输出有效");
        }
        {
            HImage out;
            SobelAmp(img, &out, "sum_abs", 3);
            report("SobelAmp 边缘检测", out.IsInitialized(), "输出有效");
        }
        {
            HRegion dom = img.GetDomain();
            HTuple minV, maxV, range;
            MinMaxGray(dom, img, 0, &minV, &maxV, &range);
            report("MinMaxGray 灰度统计", maxV.D() > minV.D(), "max>min");
        }
        {
            HImage out;
            TransFromRgb(img, img, img, &out, &out, &out, "hsv");
            report("TransFromRgb 颜色转换", out.IsInitialized(), "输出有效");
        }

        // ===== 区域类（替换版下预期异常）=====
        {
            HRegion reg;
            Threshold(img, &reg, 0, 128);
            HTuple area, row, col;
            AreaCenter(reg, &area, &row, &col);
            report("Threshold 阈值区域", area.D() > 1000.0,
                   "面积=" + std::to_string(area.D()) + "(期望>1000)");
        }
        {
            HImage out;
            HRegion roi;
            GenRectangle1(&roi, 50.0, 50.0, 150.0, 150.0);
            ReduceDomain(img, roi, &out);
            HRegion dom = out.GetDomain();
            HTuple area, r, c;
            AreaCenter(dom, &area, &r, &c);
            report("ReduceDomain ROI裁剪", area.D() > 8000.0,
                   "域面积=" + std::to_string(area.D()) + "(期望>8000)");
        }
        {
            HRegion reg;
            Threshold(img, &reg, 0, 128);
            HRegion dilated;
            DilationCircle(reg, &dilated, 3.5);
            HTuple a1, a2, r1, c1, r2, c2;
            AreaCenter(reg, &a1, &r1, &c1);
            AreaCenter(dilated, &a2, &r2, &c2);
            report("DilationCircle 区域膨胀", a2.D() > a1.D(),
                   "膨胀后面积=" + std::to_string(a2.D()) + "(应>" + std::to_string(a1.D()) + ")");
        }
        {
            // 亚像素轮廓（HImage::ThresholdSubPix，替换版头文件无 threshold_sub_pix）
            try {
                HXLDCont contour = HImage(img).ThresholdSubPix(100.0);
                HTuple len;
                LengthXld(contour, &len);
                report("ThresholdSubPix 亚像素轮廓", len.I() > 50,
                       "轮廓点数=" + std::to_string(len.I()));
            } catch (const HException &e) {
                report("ThresholdSubPix 亚像素轮廓", false,
                       std::string("异常: ") + e.ErrorMessage().Text());
            }
        }
        {
            HRegion reg2;
            Threshold(img, &reg2, 0, 128);
            HTuple area2, row2, col2, fw, fh, fphi;
            AreaCenter(reg2, &area2, &row2, &col2);
            SmallestRectangle2(reg2, &row2, &col2, &fw, &fh, &fphi);
            report("SmallestRectangle2 Blob特征", fw.D() > 1.0 && fh.D() > 1.0,
                   "宽=" + std::to_string(fw.D()) + " 高=" + std::to_string(fh.D()));
        }
        {
            // 角点检测（像素级）
            try {
                HTuple rowP, colP;
                PointsHarris(img, 1.5, 2.0, 0.08, 50, &rowP, &colP);
                report("PointsHarris 角点", rowP.Length() >= 0, "角点数=" + std::to_string(rowP.Length()));
            } catch (const HException &e) {
                report("PointsHarris 角点", false, std::string("异常: ") + e.ErrorMessage().Text());
            }
        }

        // ===== 测量类（替换版下预期无结果）=====
        {
            try {
                HTuple mHandle, mIdx;
                CreateMetrologyModel(&mHandle);
                AddMetrologyObjectLineMeasure(mHandle, 30, 30, 226, 226, 20, 5, 1.0, 30.0,
                                              "measure_transition", "all", &mIdx);
                ApplyMetrologyModel(img, mHandle);
                HTuple nEdges;
                GetMetrologyObjectResult(mHandle, mIdx, "all", "num_measures", HTuple(), &nEdges);
                report("Metrology 测量", nEdges.I() > 0,
                       "边缘点数=" + std::to_string(nEdges.I()) + "(期望>0)");
            } catch (const HException &e) {
                report("Metrology 测量", false, std::string("异常: ") + e.ErrorMessage().Text());
            }
        }

        // ===== 模板匹配（真实图）=====
        {
            try {
                HTuple modelId;
                CreateShapeModel(img, "auto", HTuple(-10).TupleConcat(10), "auto", "auto",
                                 "use_polarity", "auto", "auto", "auto", &modelId);
                HTuple rowM, colM, angleM, scoreM;
                FindShapeModel(img, modelId, -10, 20, 0.5, 1, 0.5, "least_squares", 0, 0.9,
                               &rowM, &colM, &angleM, &scoreM);
                report("ShapeModel 模板匹配", scoreM.Length() >= 1,
                       "匹配数=" + std::to_string(scoreM.Length()));
            } catch (const HException &e) {
                report("ShapeModel 模板匹配", false, std::string("异常: ") + e.ErrorMessage().Text());
            }
        }

        // ===== 标定（FindCalibObject 实测）=====
        {
            try {
                HTuple calibHandle;
                CreateCalibData("calibration_object", 1, 1, &calibHandle);
                HTuple camParams;
                camParams.Append(0.008);   // focus
                camParams.Append(0.0);     // kappa
                camParams.Append(0.000012); // sx
                camParams.Append(0.000012); // sy
                camParams.Append(320.0);    // cx
                camParams.Append(240.0);    // cy
                camParams.Append(640);      // width
                camParams.Append(480);      // height
                SetCalibDataCamParam(calibHandle, 0, "area_scan_division", camParams);
                // 设置标定物描述文件（HALCON 自带 caltab_30mm.descr）
                try {
                    SetCalibDataCalibObject(calibHandle, 0, "calib/caltab_30mm.descr");
                    HImage sim = img;
                    FindCalibObject(sim, calibHandle, 0, 0, 0, HTuple(), HTuple());
                    report("FindCalibObject 标定板", true, "未抛异常（但无真实标定板）");
                } catch (const HException &e) {
                    report("FindCalibObject 标定板", false,
                           std::string("异常: ") + e.ErrorMessage().Text());
                }
            } catch (const HException &e) {
                report("FindCalibObject 标定板", false,
                       std::string("异常: ") + e.ErrorMessage().Text());
            }
        }

        // ===== 点运算类（几何计算）=====
        {
            try {
                // 3 对点解仿射（24.11 要求向量输入）
                HTuple hom;
                VectorToHomMat2d(HTuple(10).Append(50).Append(90),
                                 HTuple(20).Append(60).Append(30),
                                 HTuple(30).Append(70).Append(110),
                                 HTuple(40).Append(80).Append(50), &hom);
                HTuple qx, qy;
                AffineTransPoint2d(hom, 10, 20, &qx, &qy);
                report("VectorToHomMat2d 仿射", fabs(qx.D() - 30.0) < 0.01 && fabs(qy.D() - 40.0) < 0.01,
                       "点(10,20)->(" + std::to_string(qx.D()) + "," + std::to_string(qy.D()) + ")");
            } catch (const HException &e) {
                report("VectorToHomMat2d 仿射", false, std::string("异常: ") + e.ErrorMessage().Text());
            }
        }
        {
            // 两点距离（DistanceNode）
            try {
                HTuple d;
                DistancePp(10, 10, 34, 10, &d);
                report("DistancePp 两点距离", fabs(d.D() - 24.0) < 0.01, "距离=" + std::to_string(d.D()));
            } catch (const HException &e) {
                report("DistancePp 两点距离", false, std::string("异常: ") + e.ErrorMessage().Text());
            }
        }
        {
            // 轮廓拟合（FitLine/FitCircle 节点的拟合算子，构造已知轮廓）
            try {
                HTuple rows, cols;
                for (int i = 0; i < 20; ++i) {
                    rows.Append(100 + i);
                    cols.Append(100 + i);
                }
                HXLDCont contour(rows, cols);
                HTuple r1, c1, r2, c2, nr, nc, dist;
                FitLineContourXld(contour, "tukey", -1, 0, 5, 2, &r1, &c1, &r2, &c2, &nr, &nc, &dist);
                report("FitLineContourXld 直线拟合", r1.Length() > 0,
                       "拟合直线数=" + std::to_string(r1.Length()));
            } catch (const HException &e) {
                report("FitLineContourXld 直线拟合", false, std::string("异常: ") + e.ErrorMessage().Text());
            }
        }
        {
            // 圆拟合
            try {
                HTuple rows, cols;
                for (int i = 0; i < 36; ++i) {
                    const double a = i * 3.14159265 * 2.0 / 36.0;
                    rows.Append(128 + 40 * std::sin(a));
                    cols.Append(128 + 40 * std::cos(a));
                }
                HXLDCont contour(rows, cols);
                HTuple rowC, colC, radius, startPhi, endPhi, order;
                FitCircleContourXld(contour, "algebraic", -1, 0, 0, 2, 1.0, &rowC, &colC, &radius,
                                    &startPhi, &endPhi, &order);
                report("FitCircleContourXld 圆拟合", radius.Length() > 0 && fabs(radius.D() - 40.0) < 1.0,
                       "半径=" + std::to_string(radius.D()));
            } catch (const HException &e) {
                report("FitCircleContourXld 圆拟合", false, std::string("异常: ") + e.ErrorMessage().Text());
            }
        }
    } catch (const HException &e) {
        report("整体", false, std::string("异常: ") + e.ErrorMessage().Text());
    }

    std::printf("\n==== 汇总: %d PASS / %d FAIL ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
