#pragma once
// 简单目标跟踪器：帧间模板匹配（TM_CCOEFF_NORMED + 局部搜索窗口）
// 无 OpenCV contrib tracking 依赖；适合工业场景中目标位置跟随
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <cmath>

namespace SimpleTracker {

class Tracker
{
public:
    Tracker() = default;

    /// 初始化：截取 gray 图中 rect 区域为模板
    bool init(const cv::Mat &gray, const cv::Rect &rect)
    {
        if (gray.empty() || rect.width < 4 || rect.height < 4)
            return false;
        cv::Rect clipped = rect & cv::Rect(0, 0, gray.cols, gray.rows);
        if (clipped.width < 4 || clipped.height < 4)
            return false;
        m_template = gray(clipped).clone();
        m_lastRect = clipped;
        // 模板方差过低（纯色/低纹理）时 CCOEFF 归一化不稳定，改用 SQDIFF
        cv::Scalar mean, stddev;
        cv::meanStdDev(m_template, mean, stddev);
        m_useSqdiff = (stddev[0] < 1.0);
        m_initialized = true;
        return true;
    }

    /// 更新：在上一位置 ± margin 窗口内搜索模板，输出最新目标框与得分
    bool update(const cv::Mat &gray, cv::Rect &outRect, double &outScore,
                int margin = 50)
    {
        if (!m_initialized || gray.empty())
            return false;
        cv::Rect searchRect = m_lastRect;
        searchRect.x = std::max(0, m_lastRect.x - margin);
        searchRect.y = std::max(0, m_lastRect.y - margin);
        searchRect.width = std::min(gray.cols - searchRect.x,
                                    m_lastRect.width + 2 * margin);
        searchRect.height = std::min(gray.rows - searchRect.y,
                                     m_lastRect.height + 2 * margin);
        if (searchRect.width < m_template.cols || searchRect.height < m_template.rows)
            return false;
        const cv::Mat search = gray(searchRect);

        cv::Mat res;
        cv::Point bestLoc;
        double bestVal = 0.0;
        if (m_useSqdiff) {
            cv::matchTemplate(search, m_template, res, cv::TM_SQDIFF_NORMED);
            double minVal = 0.0;
            cv::minMaxLoc(res, &minVal, nullptr, &bestLoc, nullptr);
            bestVal = 1.0 - minVal;  // 转成"得分"语义
        } else {
            cv::matchTemplate(search, m_template, res, cv::TM_CCOEFF_NORMED);
            double maxVal = 0.0;
            cv::minMaxLoc(res, nullptr, &maxVal, nullptr, &bestLoc);
            bestVal = maxVal;
        }

        outScore = bestVal;
        // 模板左上角在搜索窗口内的位置 -> 全局坐标
        const int gx = searchRect.x + bestLoc.x;
        const int gy = searchRect.y + bestLoc.y;
        outRect = cv::Rect(gx, gy, m_template.cols, m_template.rows);
        m_lastRect = outRect;
        return true;
    }

    bool initialized() const { return m_initialized; }
    cv::Rect lastRect() const { return m_lastRect; }

private:
    cv::Mat m_template;
    cv::Rect m_lastRect;
    bool m_initialized = false;
    bool m_useSqdiff = false;  /// 低纹理模板使用 SQDIFF 匹配
};

}  // namespace SimpleTracker
