#include "lsp_server.h" // 引入 LSP 服务器类头文件
#include <cstdio>       // 引入 C 标准输入输出库 (用于 setbuf)
#include <exception>    // 引入标准异常库
#include <iostream>     // 引入标准输入输出流库 (用于底层错误)
#include <locale>       // 引入区域设置库

// --- 引入 plog 日志库相关头文件 ---
#include <plog/Log.h>
// 选择一个 Appender，例如输出到控制台 (stderr)
#include <plog/Appenders/ConsoleAppender.h>
// 选择一个 Formatter，例如 TXT 格式
#include <plog/Formatters/TxtFormatter.h>
// 如果需要初始化器，例如滚动文件初始化器
#include "plog/Appenders/ConsoleAppender.h"
#include "plog/Initializers/RollingFileInitializer.h"
#include "plog/Log.h"
#include <plog/Initializers/RollingFileInitializer.h>

// 如果在 Windows 上编译，引入特定头文件用于设置二进制模式
#ifdef _WIN32
#include <fcntl.h> // 包含 _O_BINARY
#include <io.h>    // 包含 _setmode
#endif

void setup_plog_logger() {
  // 配置日志文件路径和滚动选项
  // 例如：logs/sptscript_lsp.log, 最大 5MB, 保留 3 个文件
  static plog::RollingFileAppender<plog::TxtFormatter> fileAppender(
      "C:/Users/ftp/Desktop/sptscript-lsp/cmake-build-debug/logs/sptscript_lsp.log",
      1024 * 1024 * 5, 3);
  //    static plog::ConsoleAppender<plog::TxtFormatter> consoleAppender; //
  plog::init(plog::info, &fileAppender); // 只输出到文件

  // 可以用 PLOG_INFO 等宏记录初始化完成
  PLOGI << "Plog logger initialized successfully."; // I 表示 Info 级别
}
#if 0

/**
 * @brief LSP 服务器主函数入口点。
 * @param argc 命令行参数数量。
 * @param argv 命令行参数数组。
 * @return 程序退出代码 (0 表示成功)。
 */
int main(int argc, char* argv[]) {
    // --- 初始化阶段 ---

    // 1. 初始化 plog 日志库
    setup_plog_logger();

    PLOG_INFO << "SptScript LSP 服务器正在启动..."; // 使用 plog 记录启动信息

    // 2. (可选但推荐) 设置 locale 以支持 UTF-8
    try {
        PLOG_VERBOSE << "尝试设置 Locale..."; // 使用 VERBOSE 级别记录细节
        // 使用 C 风格的 setlocale 尝试设置
        // (请根据您的系统环境调整 locale 字符串)
        if (std::setlocale(LC_ALL, ".UTF-8") == nullptr &&
            std::setlocale(LC_ALL, "C.UTF-8") == nullptr &&
            std::setlocale(LC_ALL, "en_US.UTF-8") == nullptr && // 备用选项
            std::setlocale(LC_ALL, "") == nullptr // 依赖系统默认
                )
        {
            PLOG_WARNING << "无法自动设置 UTF-8 Locale，非 ASCII 字符处理可能不正确。";
        }
        PLOG_INFO << "当前 Locale 设置为: " << (std::setlocale(LC_ALL, nullptr) ? std::setlocale(LC_ALL, nullptr) : "<未知>");

    } catch (const std::exception& e) {
        PLOG_WARNING << "设置 locale 时发生异常: " << e.what();
    }

    // 3. (关键!) 配置标准输入输出流
    PLOG_INFO << "正在配置标准输入输出流...";
#ifdef _WIN32
    // 在 Windows 上，设置 stdin 和 stdout 为二进制模式
    if (_setmode(_fileno(stdin), _O_BINARY) == -1) {
        PLOG_FATAL << "无法将 stdin 设置为二进制模式。错误: " << strerror(errno);
        return 1;
    }
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        PLOG_FATAL << "无法将 stdout 设置为二进制模式。错误: " << strerror(errno);
        return 1;
    }
    PLOG_INFO << "已将 stdin 和 stdout 设置为二进制模式 (Windows)。";
#endif
    // 禁用 stdout 和 stderr 的缓冲，确保响应和日志能立即发送
    if (setvbuf(stdout, nullptr, _IONBF, 0) != 0) {
        PLOG_WARNING << "无法禁用 stdout 的缓冲。";
    }
    if (setvbuf(stderr, nullptr, _IONBF, 0) != 0) {
        // 注意：如果 plog 配置为输出到 stderr，禁用 stderr 缓冲可能影响 plog
        // 但对于 LSP 来说，stderr 通常用于日志，无缓冲通常更好
        // 如果 plog 出现问题，可以考虑不禁用 stderr 缓冲
        PLOG_WARNING << "无法禁用 stderr 的缓冲。";
    }
    PLOG_INFO << "标准输入输出流配置完成。";

    std::ios_base::sync_with_stdio(false);
    // --- 服务器启动与运行 ---
    PLOG_INFO << "创建 LSP 服务器实例...";
    LspServer server; // 创建服务器对象

    PLOG_INFO << "开始运行服务器主循环...";
    // 运行服务器的主消息循环，并在最外层捕获任何未处理的异常
    try {
        server.run(); // 启动服务器，此函数将阻塞直到服务器退出
        PLOG_INFO << "服务器主循环正常结束。";
        return 0; // 正常退出码
    } catch (const std::exception& e) {
        // 记录未能处理的严重错误
        PLOG_FATAL << "服务器运行期间发生未捕获的标准异常: " << e.what();
        return 1; // 返回错误码
    } catch (...) {
        // 捕获所有其他类型的未知异常
        PLOG_FATAL << "服务器运行期间发生未知类型的未捕获异常。";
        return 1; // 返回错误码
    }
}
#else

#include "json.hpp" // 确保 include 路径正确
#include <chrono>   // 用于添加延时 (调试用)
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread> // 用于添加延时 (调试用)
#include <vector>

// 如果在 Windows 上
#ifdef _WIN32
#include <fcntl.h> // for _O_BINARY
#include <io.h>    // for _setmode
#include <windows.h>
#endif
// 如果在 Linux/macOS 上
#ifdef __unix__
#include <unistd.h>
#define MAX_PATH 260
#endif

using json = nlohmann::json;

// --- 工具函数 ---
// 确保日志立即刷新
void log_error(const std::string &message) {
  PLOGI << "[Server ERROR] " << message << std::endl << std::flush;
}

void log_warn(const std::string &message) {
  PLOGI << "[Server WARN] " << message << std::endl << std::flush;
}

void log_info(const std::string &message) {
  PLOGI << "[Server INFO] " << message << std::endl << std::flush;
}

void log_debug(const std::string &message) {
  // 可以通过注释掉下面的输出来控制是否显示 DEBUG 日志
  PLOGI << "[Server DEBUG] " << message << std::endl << std::flush;
}

// 发送 JSON 消息到 stdout
void sendMessage(const json &msg) {
  std::string msg_str = msg.dump();
  log_debug("Preparing to send message: " + msg_str);
  // 必须严格按照 LSP 协议格式发送
  std::cout << "Content-Length: " << msg_str.length() << "\r\n";
  // 根据 LSP 规范，Content-Type 是可选的，但 VS Code 常常需要它
  std::cout << "Content-Type: application/vscode-jsonrpc; charset=utf-8\r\n";
  std::cout << "\r\n";                // Header 和 Body 之间的空行是必须的
  std::cout << msg_str << std::flush; // ！！！极其重要：必须刷新 stdout ！！！
  log_info("Message sent. Length: " + std::to_string(msg_str.length()));
}

// --- 消息处理函数
json handleInitialize(const json &request) {
  log_info("Handling 'initialize' request id: " + request["id"].dump());
  json capabilities = {{"textDocumentSync",
                        {
                            {"openClose", true}, {"change", 0} // 0 = None
                        }}};
  json result = {{"capabilities", capabilities}};
  json response = {{"jsonrpc", "2.0"}, {"id", request["id"]}, {"result", result}};
  return response;
}

void handleDidOpen(const json &notification) {
  log_info("Handling 'textDocument/didOpen' notification.");
  if (notification.contains("params") && notification["params"].contains("textDocument")) {
    // 获取文档信息
    const auto &textDocument = notification["params"]["textDocument"];
    std::string uri = textDocument.value("uri", "unknown_uri"); // 获取 URI
    std::string text = textDocument.value("text", "");          // 获取文本内容
    int version = textDocument.value("version", 0);             // 获取版本
    std::string langId = textDocument.value("languageId", "");  // 获取语言 ID

    log_info("  URI: " + uri);
    log_info("  LanguageID: " + langId);
    log_info("  Version: " + std::to_string(version));
    log_info("  Text length: " + std::to_string(text.length()));
    log_debug("  Initial text: " + text.substr(0, 100) +
              (text.length() > 100 ? "..." : "")); // 打印部分文本

    // *******************************************
    // ** 在这里添加你的核心逻辑 **
    // 例如：将文档内容存储在内存中
    // document_store[uri] = text; // 假设 document_store 是一个 std::map<std::string, std::string>
    //
    // 或者：开始解析文档内容
    // auto ast = parseDocument(text);
    //
    // 或者：进行初步的静态分析并发送诊断信息
    // auto diagnostics = analyzeDocument(text);
    // publishDiagnostics(uri, diagnostics); // (需要额外实现 publishDiagnostics)
    // *******************************************

  } else {
    log_warn("Malformed 'textDocument/didOpen' notification.");
  }
  // 通知不需要响应
}

#include <chrono>
#include <thread>

// --- 主函数 ---
int main(int argc, char *argv[]) {
  setup_plog_logger();
  log_info("--------------------------------------------------");
  log_info("Server process started.");
  log_info("--------------------------------------------------");

  // (可选) 打印工作目录 - 帮助诊断路径问题
  char cwd[MAX_PATH];
#ifdef _WIN32
  if (GetCurrentDirectoryA(MAX_PATH, cwd)) {
    log_info("Current Working Directory: " + std::string(cwd));
  } else {
    log_warn("Failed to get Current Working Directory.");
  }
  // !!! 重要: 在 Windows 上，为 stdin/stdout 设置二进制模式 !!!
  // 防止 C++ 标准库对 \n 和 \r\n 进行转换，LSP 需要精确的 \r\n
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
  log_info("Set stdin/stdout to binary mode (Windows).");
#elif __unix__
  if (getcwd(cwd, sizeof(cwd)) != nullptr) {
    log_info("Current Working Directory: " + std::string(cwd));
  } else {
    log_warn("Failed to get Current Working Directory.");
  }
#endif

  // 禁用 C++ stdio 与 C stdio 的同步，可能提高性能
  std::ios_base::sync_with_stdio(false);
  // 解除 cin 和 cout 的绑定，可能提高性能 (但也可能导致输出顺序问题，调试时可注释掉)
  // std::cin.tie(nullptr);
  log_info("iostream sync_with_stdio(false) called.");

  bool shutdown_received = false;

  log_info("Entering main communication loop...");
  LspServer ls;
//  std::this_thread::sleep_for(std::chrono::seconds(8));
  while (true) { // 使用更健壮的循环条件
    // log_debug("------------------ New Loop Iteration ------------------");

    // 1. 读取 Headers
    long long contentLength = -1;
    std::string line;
    // log_debug("Attempting to read headers...");
    // std::this_thread::sleep_for(std::chrono::seconds(8));
    try {
      while (true) {
        // 注意：直接用 std::getline 可能在读取空行或只有\r的行时行为不确定
        // 更健壮的方法是逐字符读取，但这会更复杂。我们先尝试 getline。
        if (!std::getline(std::cin, line)) {
          // 如果 getline 失败 (例如 EOF 或流错误)
          if (std::cin.eof()) {
            // log_info("EOF reached while reading headers. Exiting loop.");
          } else {
            log_error("std::getline failed while reading headers. Stream state: fail=" +
                      std::to_string(std::cin.fail()) + ", bad=" + std::to_string(std::cin.bad()));
          }
          goto end_loop; // 跳出外层 while 循环
        }

        // 移除行尾可能的回车符 \r (跨平台兼容性)
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
          log_debug("Removed trailing '\\r'. Line is now: [" + line + "]");
        } else {
          log_debug("Raw header line received: [" + line + "]");
        }

        // 检查是否是空行 (Header 结束标志)
        if (line.empty()) {
          // log_debug("Empty line received, headers finished.");
          break; // 跳出读取 header 的循环
        }

        // 解析 Content-Length (忽略大小写)
        std::string header_key_lower = "content-length: ";
        std::string line_lower = line;
        // 转为小写比较
        std::transform(line_lower.begin(), line_lower.end(), line_lower.begin(), ::tolower);

        size_t pos = line_lower.find(header_key_lower);
        if (pos == 0) { // 确保是行首匹配
          try {
            contentLength = std::stoll(line.substr(header_key_lower.length()));
            // log_info("Parsed Content-Length: " + std::to_string(contentLength));
          } catch (const std::invalid_argument &ia) {
            log_error("Invalid argument converting Content-Length: " +
                      line.substr(header_key_lower.length()));
            contentLength = -1; // 重置为无效
          } catch (const std::out_of_range &oor) {
            log_error("Out of range converting Content-Length: " +
                      line.substr(header_key_lower.length()));
            contentLength = -1; // 重置为无效
          }
        } else {
          // 忽略其他 Header，例如 Content-Type
          // log_debug("Ignoring header line: " + line);
        }
      } // 结束读取 Header 的循环

      // 检查 Content-Length 是否有效
      if (contentLength < 0) {
        log_error("Invalid or missing Content-Length header received.");
        // 这里可以决定是继续尝试读取下一条消息还是退出
        continue; // 尝试下一次循环迭代
      }
      if (contentLength == 0) {
        log_warn("Content-Length is 0. Skipping body read.");
        // 虽然长度为0，但仍然可能有方法调用（如 exit 通知），需要解析空body或继续
      }

      // 2. 读取 Body (JSON content)
      std::vector<char> buffer(contentLength); // 创建足够大的缓冲区
      if (contentLength > 0) {
        log_debug("Attempting to read body of length: " + std::to_string(contentLength));
        // 直接从 std::cin 读取指定长度的字节
        std::cin.read(buffer.data(), contentLength);

        // 检查读取操作是否成功，并且是否读到了期望的字节数
        if (!std::cin || std::cin.gcount() != contentLength) {
          log_error("Failed to read expected body length. Read " +
                    std::to_string(std::cin.gcount()) + " bytes. Stream state: fail=" +
                    std::to_string(std::cin.fail()) + ", bad=" + std::to_string(std::cin.bad()) +
                    ", eof=" + std::to_string(std::cin.eof()));
          // 如果读取失败，可能流已损坏，难以恢复，选择退出
          goto end_loop;
        }
        // log_debug("Successfully read " + std::to_string(std::cin.gcount()) + " bytes for body.");
      } else {
        // log_debug("Skipped reading body as ContentLength is 0.");
      }

      // 3. 解析 JSON
      std::string json_str(buffer.begin(), buffer.end());
      // log_debug("Raw JSON received: " + json_str); // 调试时打印原始 JSON
      json request_msg;
      try {
        if (json_str.empty() && contentLength == 0) {
          log_warn("Received empty body with ContentLength=0. Assuming no JSON payload.");
          // 对于某些通知(如 exit)，可能没有 params，空 payload 是可能的
          // 但这里我们假设大多数消息需要 payload，如果为空则跳过处理
          continue;
        } else if (json_str.empty() && contentLength > 0) {
          log_error("Read empty body despite ContentLength > 0. Aborting processing.");
          continue;
        }
        request_msg = json::parse(json_str);
        log_debug("JSON parsed successfully.");
      } catch (const json::parse_error &e) {
        log_error("JSON Parse Error: " + std::string(e.what()) + ". Raw JSON was: " + json_str);
        continue; // 忽略无法解析的消息，尝试读取下一条
      }

      // 4. 处理消息
      if (!request_msg.contains("method")) {
        // 可能是响应消息，也可能是格式错误
        if (request_msg.contains("id") &&
            (request_msg.contains("result") || request_msg.contains("error"))) {
          log_warn("Received a Response message, which the server shouldn't get. Ignoring.");
        } else {
          log_warn("Received message without 'method' field. Ignoring.");
        }
        continue;
      }
      std::string method = request_msg["method"];
      ls.handleMessage(request_msg);

    } catch (const std::exception &e) {
      log_error("!!! Exception caught in main loop: " + std::string(e.what()));
      // 考虑是否要继续循环或退出
    } catch (...) {
      log_error("!!! Unknown exception caught in main loop.");
      // 考虑是否要继续循环或退出
    }

    //(可选) 添加少量延时，防止CPU满载 (如果循环意外地空转)
    // std::this_thread::sleep_for(std::chrono::milliseconds(10));

  } // 结束主 while 循环

end_loop: // 循环结束或出错跳转点
  log_info("--------------------------------------------------");
  log_info("Exiting server process. Final state: shutdown_received=" +
           std::to_string(shutdown_received));
  log_info("--------------------------------------------------");

  // 根据 LSP 规范，exit code 应该是 0 (如果收到 shutdown) 或 1 (如果未收到 shutdown)
  return shutdown_received ? 0 : 1;
}
#endif