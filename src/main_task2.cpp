// ============================================================================
// 任务二：识别图中的装甲板灯条并框选（"不限定方法"，这里用传统视觉实现）
//
// 难点：原图是一张很暗的室内照片（平均灰度仅 15），装甲板本身几乎不可见，
//       能稳定利用的只有"灯条自发光"这一先验：高亮度 + 橙红色 + 竖直细长。
//
// 流程：
//   1. 灯条候选：BGR -> HSV，取"高 V + 中高 S + 橙红色相"的区域。
//      灯条是自发光，V 接近 255，阈值可以和背景干净地分开；
//      纵向闭运算（7x3 竖条核）把灯条被反光/遮挡切断的碎片重新连成一根。
//   2. 形态筛选：按面积、最小高度、长宽比（h/w >= 1.8）去掉反光点、地面倒影和横向光斑。
//   3. 灯条配对成装甲板：同一块装甲板上的两根灯条高度接近、中心几乎水平、间距不超过几倍灯高；
//      用"代价最小"的贪心匹配，避免错配。
//   4. 绘制：灯条画绿色细框，配对成功的装甲板画红色粗框（外接正矩形，略有外扩）。
//      可视化底图先做一次 gamma 提亮（gamma=0.5），否则结果图一片黑、看不清框。
// ============================================================================
#include "opencv_compat.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "imgio.hpp"

namespace {

// ---- 灯条颜色阈值（橙色高亮：H 0-35 覆盖橙红；V 门槛保证只留自发光部分）----
const cv::Scalar kBarLo(0, 90, 140), kBarHi(35, 255, 255);

// ---- 灯条形态约束 ----
const int kMinBarArea = 60;     // 最小面积
const int kMinBarHeight = 15;   // 最小高度
const double kMinBarAspect = 1.8;  // 高/宽 下限，灯条是竖条

// ---- 配对约束 ----
const double kMaxDyRatio = 0.5;    // 两灯条中心高度差 / 平均灯高
const double kMinHeightRatio = 0.5;  // 两灯条高度比下限（近大远小、遮挡会差一点，但不该差太多）
const double kMaxGapRatio = 3.0;   // 两灯条间距 / 平均灯高 上限
const double kIdealGapRatio = 1.8; // 经验值：正常装甲板两灯条间距约为灯高的 1.5~2 倍
const int kArmorPad = 4;           // 装甲板框外扩像素

struct LightBar {
    cv::Rect rect;
    double cx = 0, cy = 0;
};

// gamma 提亮，仅用于结果图可视化
cv::Mat brightenForDisplay(const cv::Mat& src, double gamma) {
    cv::Mat lut(1, 256, CV_8U);
    for (int i = 0; i < 256; ++i) {
        lut.at<uchar>(0, i) = cv::saturate_cast<uchar>(std::pow(i / 255.0, gamma) * 255.0);
    }
    cv::Mat out;
    cv::LUT(src, lut, out);
    return out;
}

// 提取灯条候选
std::vector<LightBar> findLightBars(const cv::Mat& bgr, cv::Mat* maskOut) {
    cv::Mat hsv, mask;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, kBarLo, kBarHi, mask);

    // 纵向闭运算：把同一根灯条被切断的部分连起来
    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 7));
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

    cv::Mat labels, stats, centroids;
    const int n = cv::connectedComponentsWithStats(mask, labels, stats, centroids, 8);

    std::vector<LightBar> bars;
    for (int i = 1; i < n; ++i) {  // 0 是背景
        const int x = stats.at<int>(i, cv::CC_STAT_LEFT);
        const int y = stats.at<int>(i, cv::CC_STAT_TOP);
        const int w = stats.at<int>(i, cv::CC_STAT_WIDTH);
        const int h = stats.at<int>(i, cv::CC_STAT_HEIGHT);
        const int area = stats.at<int>(i, cv::CC_STAT_AREA);

        if (area < kMinBarArea || h < kMinBarHeight) continue;
        if (h < kMinBarAspect * std::max(w, 1)) continue;  // 过滤横条与方块状光斑

        LightBar b;
        b.rect = cv::Rect(x, y, w, h);
        b.cx = static_cast<double>(x) + w / 2.0;
        b.cy = static_cast<double>(y) + h / 2.0;
        bars.push_back(b);
    }
    std::sort(bars.begin(), bars.end(),
              [](const LightBar& a, const LightBar& b) { return a.cx < b.cx; });

    if (maskOut) *maskOut = mask;
    return bars;
}

// 贪心配对：每根灯条只归入一块装甲板，代价 = 高度差 + 间距偏离 + 高度不相似
std::vector<std::pair<int, int>> matchArmors(const std::vector<LightBar>& bars) {
    std::vector<std::pair<int, int>> pairs;
    std::vector<bool> used(bars.size(), false);

    for (size_t i = 0; i < bars.size(); ++i) {
        if (used[i]) continue;
        int best = -1;
        double bestCost = 1e9;

        for (size_t j = i + 1; j < bars.size(); ++j) {
            if (used[j]) continue;
            const double meanH = (bars[i].rect.height + bars[j].rect.height) / 2.0;
            const double dy = std::fabs(bars[i].cy - bars[j].cy);
            const double heightRatio = std::min(bars[i].rect.height, bars[j].rect.height) /
                                       static_cast<double>(std::max(bars[i].rect.height, bars[j].rect.height));
            // 左灯条右边界到右灯条左边界的水平间距
            const double gap = bars[j].rect.x - (bars[i].rect.x + bars[i].rect.width);

            if (dy > kMaxDyRatio * meanH) continue;        // 不在同一水平线上
            if (heightRatio < kMinHeightRatio) continue;   // 高度差太多，不是同一块板
            if (gap < 0 || gap > kMaxGapRatio * meanH) continue;  // 间距不合理

            const double cost = dy / meanH + 0.5 * std::fabs(gap / meanH - kIdealGapRatio) +
                                (1.0 - heightRatio);
            if (cost < bestCost) {
                bestCost = cost;
                best = static_cast<int>(j);
            }
        }

        if (best >= 0) {
            used[i] = true;
            used[best] = true;
            pairs.emplace_back(static_cast<int>(i), best);
        }
    }
    return pairs;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string inPath =
        argc > 1 ? argv[1]
                 : imgio::pickExisting({"images/task2_armor.jpg", "../images/task2_armor.jpg",
                                        "../../images/task2_armor.jpg"},
                                       "images/task2_armor.jpg");
    const std::string outDir =
        argc > 2 ? argv[2]
                 : imgio::pickExisting({"results", "../results", "../../results"}, "results");

    const cv::Mat img = imgio::imreadU8(inPath);
    if (img.empty()) {
        std::cerr << "读取图像失败: " << inPath << "\n"
                  << "用法: task2_armor [输入图像] [输出目录]\n";
        return -1;
    }
    std::cout << "输入图像: " << inPath << "  尺寸: " << img.cols << "x" << img.rows << "\n";

    cv::Mat barMask;
    const std::vector<LightBar> bars = findLightBars(img, &barMask);

    std::cout << "检测到灯条: " << bars.size() << " 根\n";
    for (size_t i = 0; i < bars.size(); ++i) {
        const cv::Rect& r = bars[i].rect;
        std::cout << "  灯条#" << i << " x=" << r.x << " y=" << r.y << " w=" << r.width
                  << " h=" << r.height << " 长宽比=" << (r.height / static_cast<double>(r.width))
                  << "\n";
    }

    const std::vector<std::pair<int, int>> pairs = matchArmors(bars);

    cv::Mat vis = brightenForDisplay(img, 0.5);  // 提亮后再画框，结果图才看得清

    // 灯条：绿色细框
    for (const auto& b : bars) {
        cv::rectangle(vis, b.rect, cv::Scalar(0, 255, 0), 1, cv::LINE_AA);
    }

    // 装甲板：红色粗框（外接正矩形）
    std::cout << "匹配到装甲板: " << pairs.size() << " 块\n";
    for (const auto& p : pairs) {
        const cv::Rect& a = bars[p.first].rect;
        const cv::Rect& b = bars[p.second].rect;
        cv::Rect armor = (a | b);  // 两块区域的并集
        armor.x = std::max(0, armor.x - kArmorPad);
        armor.y = std::max(0, armor.y - kArmorPad);
        armor.width = std::min(vis.cols - armor.x, armor.width + 2 * kArmorPad);
        armor.height = std::min(vis.rows - armor.y, armor.height + 2 * kArmorPad);

        cv::rectangle(vis, armor, cv::Scalar(0, 0, 255), 3, cv::LINE_AA);
        std::cout << "  装甲板 x=" << armor.x << " y=" << armor.y << " w=" << armor.width
                  << " h=" << armor.height << "  (灯条#" << p.first << " + #" << p.second << ")\n";
    }

    std::vector<int> lone;
    for (size_t i = 0; i < bars.size(); ++i) {
        bool paired = false;
        for (const auto& p : pairs)
            if (p.first == static_cast<int>(i) || p.second == static_cast<int>(i)) paired = true;
        if (!paired) lone.push_back(static_cast<int>(i));
    }
    if (!lone.empty()) {
        std::cout << "未配对的灯条（对侧灯条被遮挡/未点亮）: ";
        for (int idx : lone) std::cout << "#" << idx << " ";
        std::cout << "\n";
    }

    imgio::ensureDir(outDir);
    const std::string outImg = outDir + "/task2_result.png";
    const std::string outMask = outDir + "/task2_mask.png";
    if (!imgio::imwriteU8(outImg, vis)) {
        std::cerr << "写入结果失败: " << outImg << "\n";
        return -1;
    }
    imgio::imwriteU8(outMask, barMask);
    std::cout << "结果已保存: " << outImg << "\n"
              << "灯条掩膜已保存: " << outMask << "\n";
    return 0;
}
