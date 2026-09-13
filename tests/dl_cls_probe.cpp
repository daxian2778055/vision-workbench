// 用官方预训练模型实测 ReadDlModel + ApplyDlModel（DlClassificationNode 同款链路）
#include "HalconCpp.h"
#include <cstdio>
using namespace HalconCpp;
int main() {
    try {
        const char* mdl = "D:/Program Files/MVTec/HALCON-24.11-Progress-Steady/dl/pretrained_dl_classifier_compact.hdl";
        HDlModel model;
        model.ReadDlModel(mdl);
        HString t = model.GetDlModelParam("type");
        Hlong iw = model.GetDlModelParam("image_width").I();
        Hlong ih = model.GetDlModelParam("image_height").I();
        Hlong nc = model.GetDlModelParam("image_num_channels").I();
        printf("model type=%s size=%ldx%ld ch=%ld\n", t.Text(), (long)iw, (long)ih, (long)nc);
        model.SetDlModelParam(HString("runtime"), HTuple("cpu"));
        // 造一张图像
        HImage img;
        img.GenImageConst("byte", iw, ih);
        HObject pre = img;
        FullDomain(pre, &pre);
        if (nc == 3) { HObject three; Compose3(pre, pre, pre, &three); pre = three; }
        HObject scaled;
        ZoomImageSize(pre, &scaled, iw, ih, "constant");
        HObject real;
        ConvertImageType(scaled, &real, "real");
        double rangeMin = model.GetDlModelParam("image_range_min").D();
        double rangeMax = model.GetDlModelParam("image_range_max").D();
        ScaleImage(real, &real, (rangeMax-rangeMin)/255.0, rangeMin);
        HDict sample;
        sample.CreateDict();
        sample.SetDictObject(real, "image");
        HDictArray batch(&sample, 1);
        HDictArray r = model.ApplyDlModel(batch, HTuple());
        printf("apply result batch len=%ld\n", (long)r.Length());
        HDict res = r.Tools()[0];
        try {
            HTuple js;
            DictToJson(HTuple(res), HTuple(), HTuple(), &js);
            printf("  result json: %.400s\n", js.S().TextA());
        } catch (const HException &e) {
            printf("  dict_to_json 失败: %s\n", e.ErrorMessage().TextA());
        }
        const char *cands[] = {"class_ids", "scores", "confidences", "class_names",
                               "anomaly_score", "probabilities"};
        for (const char *c : cands) {
            try {
                if (res.GetDictParam("key_exists", c).I() == 1) {
                    HTuple v = res.GetDictTuple(c);
                    printf("  key=%s len=%ld val0=%s\n", c, (long)v.Length(),
                           v.Length() > 0 ? v[0].S().TextA() : "-");
                }
            } catch (...) {}
        }
        if (res.GetDictParam("key_exists", "class_ids").I() == 1) {
            HTuple ids = res.GetDictTuple("class_ids");
            HTuple conf = res.GetDictTuple("confidences");
            printf("class_ids=%ld conf=%.4f\n", (long)ids.L(), conf.D());
            printf("DL_CLS_INFER_OK\n");
        } else {
            printf("DL_CLS_NO_CLASS_IDS\n");
        }
        return 0;
    } catch (const HException& e) {
        printf("EXC #%d: %s\n", e.ErrorCode(), e.ErrorMessage().Text());
        return 1;
    }
}
