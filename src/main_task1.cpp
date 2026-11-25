// ============================================================================
// 任务一：在用图中分别用【黄色】框出蓝色矩形区域、【绿色】框出红色矩形区域
//
// 思路（纯传统视觉，无需训练）：
//   1. BGR -> HSV：颜色分割在 HSV 空间比 BGR 稳定，明暗变化只影响 V，不串色。
//   2. 蓝色：色相集中在一段区间，单区间阈值即可。
//      红色：色相跨 0° 边界（0 与 180 首尾相接），必须拆成两段再取并集，否则会漏一半。
//   3. 开运算去掉零散噪点，再取外轮廓、过滤小面积，得到每个色块的外接正矩形。
//   4. 按颜色分别用黄框 / 绿框绘制，原图尺寸输出。
// ============================================================================
#include "opencv_compat.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "imgio.hpp"

namespace {

// ---- 颜色阈值（HSV，H:0-180 S:0-255 V:0-255）----
const cv::Scalar kBlueLo(95, 70, 70), kBlueHi(140, 255, 255);
const cv::Scalar kRedLo1(0, 70, 70), kRedHi1(12, 255, 255);
const cv::Scalar kRedLo2(168, 70, 70), kRedHi2(180, 255, 255);

const int kMinArea = 200;  // 小于该面积的色块视为噪点（原图 253x134，目标面积约 900）

// 从掩膜中提取目标的外接正矩形，按左上角 x 排序，保证每次运行输出顺序一致
std::vector<cv::Rect> findBoxes(const cv::Mat& mask, int minArea, cv::Mat* cleaned) {
    cv::Mat m;
    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(mask, m, cv::MORPH_OPEN, kernel);  // 开运算：先腐蚀后膨胀，抹掉斑点

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(m, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    std::vector<cv::Rect> boxes;
    for (const auto& c : contours) {
        if (cv::contourArea(c) < minArea) continue;
        boxes.push_back(cv::boundingRect(c));
    }
    std::sort(boxes.begin(), boxes.end(),
              [](const cv::Rect& a, const cv::Rect& b) { return a.x < b.x; });

    if (cleaned) *cleaned = m;
    return boxes;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string inPath =
        argc > 1 ? argv[1]
                 : imgio::pickExisting({"images/task1_rects.png", "../images/task1_rects.png",
                                        "../../images/task1_rects.png"},
                                       "images/task1_rects.png");
    const std::string outDir =
        argc > 2 ? argv[2]
                 : imgio::pickExisting({"results", "../results", "../../results"}, "results");

    const cv::Mat img = imgio::imreadU8(inPath);
    if (img.empty()) {
        std::cerr << "读取图像失败: " << inPath << "\n"
                  << "用法: task1_color_box [输入图像] [输出目录]\n";
        return -1;
    }
    std::cout << "输入图像: " << inPath << "  尺寸: " << img.cols << "x" << img.rows << "\n";

    cv::Mat hsv;
    cv::cvtColor(img, hsv, cv::COLOR_BGR2HSV);

    // 蓝色掩膜
    cv::Mat blueMask;
    cv::inRange(hsv, kBlueLo, kBlueHi, blueMask);

    // 红色掩膜：两段色相取并集（红色跨越 0°/180° 分界线）
    cv::Mat redMaskA, redMaskB, redMask;
    cv::inRange(hsv, kRedLo1, kRedHi1, redMaskA);
    cv::inRange(hsv, kRedLo2, kRedHi2, redMaskB);
    cv::bitwise_or(redMaskA, redMaskB, redMask);

    cv::Mat blueClean, redClean;
    const std::vector<cv::Rect> blueBoxes = findBoxes(blueMask, kMinArea, &blueClean);
    const std::vector<cv::Rect> redBoxes = findBoxes(redMask, kMinArea, &redClean);

    // 在原图上绘制：蓝 -> 黄框，红 -> 绿框
    cv::Mat vis = img.clone();
    for (const auto& r : blueBoxes) {
        cv::rectangle(vis, r, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);  // 黄色 BGR(0,255,255)
    }
    for (const auto& r : redBoxes) {
        cv::rectangle(vis, r, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);  // 绿色 BGR(0,255,0)
    }

    std::cout << "蓝色矩形: " << blueBoxes.size() << " 个\n";
    for (const auto& r : blueBoxes)
        std::cout << "  [黄框] x=" << r.x << " y=" << r.y << " w=" << r.width << " h=" << r.height
                  << "\n";
    std::cout << "红色矩形: " << redBoxes.size() << " 个\n";
    for (const auto& r : redBoxes)
        std::cout << "  [绿框] x=" << r.x << " y=" << r.y << " w=" << r.width << " h=" << r.height
                  << "\n";

    imgio::ensureDir(outDir);
    const std::string outImg = outDir + "/task1_result.png";
    const std::string outMask = outDir + "/task1_mask.png";
    if (!imgio::imwriteU8(outImg, vis)) {
        std::cerr << "写入结果失败: " << outImg << "\n";
        return -1;
    }
    cv::Mat maskAll;
    cv::bitwise_or(blueClean, redClean, maskAll);
    imgio::imwriteU8(outMask, maskAll);
    std::cout << "结果已保存: " << outImg << "\n"
              << "颜色掩膜已保存: " << outMask << "\n";
    return 0;
}
