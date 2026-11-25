// imgio.hpp —— 支持中文路径的图像读写
//
// OpenCV 在 Windows 上直接 cv::imread 打开含中文的路径会失败（内部走窄字符接口，
// 用的是本地 ANSI 代码页）。这里先用标准库把文件整体读进内存，再交给 imdecode，
// 绕开这个问题；写文件同理。
//
// 文件名编码的两种情况都照顾到：
//   - 源码里写死的相对路径 / UTF-8 字符串  -> filesystem::u8path
//   - Windows 命令行传进来的参数（ANSI/GBK） -> filesystem::path
#pragma once

#include <opencv2/imgcodecs.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace imgio {
namespace detail {

inline bool readAll(const std::filesystem::path& p, std::vector<uchar>& buf) {
    std::ifstream fs(p, std::ios::binary);
    if (!fs) return false;
    buf.assign(std::istreambuf_iterator<char>(fs), std::istreambuf_iterator<char>());
    return !buf.empty();
}

inline bool writeAll(const std::filesystem::path& p, const std::vector<uchar>& buf) {
    std::ofstream fs(p, std::ios::binary);
    if (!fs) return false;
    fs.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    return fs.good();
}

}  // namespace detail

inline cv::Mat imreadU8(const std::string& path, int flags = cv::IMREAD_COLOR) {
    std::vector<uchar> buf;
    bool ok = false;
    // 先按 UTF-8 解释；失败（或字节序列不是合法 UTF-8）再按本机 ANSI 代码页解释，
    // 命令行参数通常是后者。u8path 遇到非法 UTF-8 会抛异常，所以这里要兜住。
    try {
        ok = detail::readAll(std::filesystem::u8path(path), buf);
    } catch (...) {
        ok = false;
    }
    if (!ok) {
        try {
            ok = detail::readAll(std::filesystem::path(path), buf);
        } catch (...) {
            ok = false;
        }
    }
    if (!ok) return {};
    return cv::imdecode(buf, flags);
}

inline bool imwriteU8(const std::string& path, const cv::Mat& img) {
    const std::string ext = std::filesystem::path(path).extension().string();
    if (ext.empty() || img.empty()) return false;

    std::vector<uchar> buf;
    if (!cv::imencode(ext, img, buf)) return false;

    try {
        if (detail::writeAll(std::filesystem::u8path(path), buf)) return true;
    } catch (...) {
    }
    try {
        return detail::writeAll(std::filesystem::path(path), buf);
    } catch (...) {
        return false;
    }
}

// 在当前目录 / 上一级 / 上两级中找一个存在的路径，便于从 build 目录里直接运行时也能找到图片
inline std::string pickExisting(const std::vector<std::string>& candidates,
                                const std::string& fallback) {
    std::error_code ec;
    for (const auto& c : candidates) {
        if (std::filesystem::exists(std::filesystem::u8path(c), ec)) return c;
    }
    return fallback;
}

// 创建目录（编码处理同上：先 UTF-8，再本地代码页）
inline bool ensureDir(const std::string& dir) {
    std::error_code ec;
    try {
        std::filesystem::create_directories(std::filesystem::u8path(dir), ec);
        if (!ec) return true;
    } catch (...) {
    }
    try {
        std::filesystem::create_directories(std::filesystem::path(dir), ec);
        return !ec;
    } catch (...) {
        return false;
    }
}

}  // namespace imgio
