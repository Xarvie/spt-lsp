#ifndef URI_H
#define URI_H

#include <algorithm>  // 引入算法库 (用于字符串替换)
#include <cctype>     // 引入字符处理函数 (例如 isxdigit)
#include <cstdlib>    // 引入 C 标准库函数 (例如 strtol)
#include <functional> // 引入函数对象库 (用于哈希)
#include <iostream>   // 引入标准输入输出流库 (用于日志)
#include <sstream>    // 引入字符串流 (用于解码)
#include <stdexcept>  // 引入标准异常库
#include <string>     // 引入 C++ 标准字符串库
#include <vector>     // 引入向量容器

#ifdef _WIN32
#define IS_WINDOWS 1
#else
#define IS_WINDOWS 0
#endif

// URI 处理类 (增强版)
// 重点处理 file URI 和本地路径转换，包含百分号解码
class Uri {
private:
  std::string uriString; // 存储原始 URI 字符串
  std::string path_;     // 存储转换后的本地文件系统路径
  bool valid_ = false;   // 标记 URI 是否成功解析为文件路径

  // 辅助函数：百分号解码
  static bool percentDecode(const std::string &encoded, std::string &decoded) {
    decoded.clear();
    decoded.reserve(encoded.length()); // 预分配空间

    try {
      for (size_t i = 0; i < encoded.length(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.length()) {
          char h1 = encoded[i + 1];
          char h2 = encoded[i + 2];
          if (std::isxdigit(static_cast<unsigned char>(h1)) &&
              std::isxdigit(static_cast<unsigned char>(h2))) {
            char hex_str[3] = {h1, h2, '\0'};
            char *end_ptr = nullptr;
            long int value = std::strtol(hex_str, &end_ptr, 16);
            if (*end_ptr == '\0') {
              decoded += static_cast<char>(value);
              i += 2;
            } else {
              decoded += encoded[i]; // 保留 '%'
            }
          } else {
            decoded += encoded[i]; // 保留 '%'
          }
        } else if (encoded[i] == '+') { // 处理 '+' 代表空格的情况 (非标准但常见)
          decoded += ' ';
        } else {
          decoded += encoded[i];
        }
      }
      return true;
    } catch (const std::exception &e) {
      std::cerr << "[错误] Uri::percentDecode: 发生异常: " << e.what() << std::endl;
      return false;
    }
  }

  // 静态辅助函数：将 URI 字符串转换为本地路径 (增强版)
  static bool uriToPathInternal(const std::string &uriStr, std::string &outPath) {
    outPath.clear();
    const std::string fileScheme = "file://";

    if (uriStr.rfind(fileScheme, 0) != 0) {
      std::cerr << "[错误] Uri::uriToPathInternal: 不支持的 URI 格式 (需要 'file://'): " << uriStr
                << std::endl;
      return false;
    }

    size_t startPos = fileScheme.length();
    // 处理 "file:///" (无 authority) 或 "file://hostname/" (有 authority)
    std::string authorityAndPathEncoded;
    if (uriStr.length() > startPos && uriStr[startPos] == '/') { // 针对 "file:///"
      authorityAndPathEncoded = uriStr.substr(startPos);         // path starts with '/'
    } else { // 针对 "file://hostname/..." 或 "file://hostname" (后者通常无效)
      // 查找第一个 '/' 作为 path 的开始
      size_t firstSlashAfterAuthority = uriStr.find('/', startPos);
      if (firstSlashAfterAuthority == std::string::npos) {
        // 可能是 "file://hostname" -> 无效
        std::cerr << "[错误] Uri::uriToPathInternal: 文件 URI 缺少路径部分: " << uriStr
                  << std::endl;
        return false;
      }
      // 我们通常忽略本地文件 URI 的 authority (hostname) 部分
      authorityAndPathEncoded = uriStr.substr(firstSlashAfterAuthority);
    }

    std::string decodedPath;
    if (!percentDecode(authorityAndPathEncoded, decodedPath)) {
      std::cerr << "[错误] Uri::uriToPathInternal: URI 百分号解码失败: " << uriStr << std::endl;
      return false;
    }

#if IS_WINDOWS
    // Windows 路径处理 (修正以处理 /c/Users/... 和 /C:/Users/... )
    // decodedPath is expected to be like "/c/Users/foo.txt" or "/C:/Users/foo.txt"
    // after stripping "file://" and percent-decoding.

    if (decodedPath.length() >= 3 && decodedPath[0] == '/') {
      if (std::isalpha(static_cast<unsigned char>(decodedPath[1]))) {
        if (decodedPath[2] == '/') {      // Handles "/c/Users..."
          outPath = decodedPath[1];       // 'c'
          outPath += ":\\";               // "c:\"
          if (decodedPath.length() > 3) { // Append rest of the path
            outPath += decodedPath.substr(3);
          }
          // Replace remaining forward slashes
          std::replace(outPath.begin() + 3, outPath.end(), '/', '\\');
        } else if (decodedPath[2] == ':') { // Handles "/c:/Users..."
          outPath = decodedPath.substr(1);  // "c:/Users..."
          std::replace(outPath.begin(), outPath.end(), '/', '\\');
        } else {
          std::cerr << "[错误] Uri::uriToPathInternal: Windows URI 路径格式无法识别 (盘符后既不是 "
                       "':' 也不是 '/'): "
                    << decodedPath << std::endl;
          return false;
        }
      } else if (decodedPath[0] == '/' && decodedPath[1] == '/') { // UNC path like //server/share
        // This assumes decodedPath itself is like "//server/share/file.txt"
        // which would come from a URI like "file:////server/share/file.txt"
        // or "file://server/share/file.txt" if authority was part of decodedPath
        // For simplicity, let's assume it's from file:////server/share format for now.
        outPath = "\\\\" + decodedPath.substr(2);
        std::replace(outPath.begin(), outPath.end(), '/', '\\');
      } else {
        std::cerr
            << "[错误] Uri::uriToPathInternal: Windows URI 路径格式无法识别 (非驱动器路径或UNC): "
            << decodedPath << std::endl;
        return false;
      }
    } else if (decodedPath.length() >= 2 &&
               std::isalpha(static_cast<unsigned char>(decodedPath[0])) && decodedPath[1] == ':') {
      // Handles case where decodedPath might be "C:/Users/..." directly (less common from standard
      // file URIs)
      outPath = decodedPath;
      std::replace(outPath.begin(), outPath.end(), '/', '\\');
    } else {
      std::cerr << "[错误] Uri::uriToPathInternal: Windows URI 路径格式无效: " << decodedPath
                << std::endl;
      return false;
    }

#else
    // Unix/Linux 路径处理
    if (decodedPath.empty() || decodedPath[0] != '/') {
      std::cerr << "[错误] Uri::uriToPathInternal: 无效的 Unix 文件路径格式 (未以 '/' 开头): "
                << decodedPath << std::endl;
      return false;
    }
    outPath = decodedPath;
#endif

    // 末尾分隔符处理
    if (outPath.length() > 1 && (outPath.back() == '/' || outPath.back() == '\\')) {
#if IS_WINDOWS
      // 避免移除 "C:\" 中的 '\'
      if (outPath.length() != 3 || outPath[1] != ':') {
        outPath.pop_back();
      }
#else
      if (outPath != "/") {
        outPath.pop_back();
      }
#endif
    }
    // 添加日志确认最终生成的 path_

    return true; // 转换成功
  }

public:
  // 默认构造函数
  Uri() : valid_(false) {}

  // 从字符串构造 URI 对象
  explicit Uri(const std::string &uriStr) : uriString(uriStr) {
    valid_ = uriToPathInternal(uriString, path_);
    if (!valid_) {
      std::cerr << "[警告] Uri 构造: 无法从 '" << uriString << "' 解析有效的本地文件路径。"
                << std::endl;
      path_.clear();
    }
  }

  // 获取原始 URI 字符串
  const std::string &str() const { return uriString; }

  // 获取转换后的本地文件系统路径
  const std::string &path() const { return path_; }

  // 判断 URI 是否成功解析为有效的本地文件路径
  bool isValid() const { return valid_; }

  // --- 操作符重载 ---
  bool operator<(const Uri &other) const { return uriString < other.uriString; }

  bool operator==(const Uri &other) const { return uriString == other.uriString; }

  bool operator!=(const Uri &other) const { return !(*this == other); }

  // 支持流式输出 (用于调试)
  friend std::ostream &operator<<(std::ostream &os, const Uri &uri) {
    os << uri.uriString;
    return os;
  }
};

// 为 Uri 提供哈希函数，使其能在 std::unordered_map 中作为键使用
namespace std {
template <> struct hash<Uri> {
  std::size_t operator()(const Uri &uri) const noexcept {
    return std::hash<std::string>{}(uri.str());
  }
};
} // namespace std

#endif // URI_H