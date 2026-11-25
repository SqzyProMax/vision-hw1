// opencv_compat.hpp —— 兼容 OpenCV 4.x 与 5.x 的头文件入口
//
// OpenCV 5 把形状/几何相关的函数（contourArea、boundingRect、minAreaRect 等）
// 从 imgproc 拆到了新的 geometry 模块，而 <opencv2/opencv.hpp> 不会自动包含它，
// 于是同一份代码在 5.x 下会报 "contourArea 不是 cv 的成员"。
// 这里按主版本号补上这个头，保证 4.x / 5.x 都能编译。
#pragma once

#include <opencv2/opencv.hpp>
#include <opencv2/core/version.hpp>

#if CV_VERSION_MAJOR >= 5
#include <opencv2/geometry.hpp>
#endif
