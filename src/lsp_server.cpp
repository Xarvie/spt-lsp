#include "lsp_server.h"     // 包含 LspServer 声明
#include "protocol_types.h" // 包含协议类型和 json 定义
#include <chrono>           // 用于日志时间戳
#include <iomanip>          // 用于格式化时间戳
#include <iostream>         // 用于 stdin, stdout
#include <optional>         // 用于可选值
#include <stdexcept>        // 用于运行时错误
#include <string>           // 用于字符串处理
#include <thread>           // 用于 std::this_thread::sleep_for (可选)

// --- 引入 plog 日志库 ---
#include <plog/Log.h>

// 使用 nlohmann/json 命名空间
using json = nlohmann::json;

void markTimePoint(const std::string &message) {
  // --- 核心部分：使用 static 变量 ---
  // last_timepoint: 存储上一次调用此函数的时间点。
  //               static 确保它在多次调用间保持值。只初始化一次。
  static std::chrono::steady_clock::time_point last_timepoint;
  // is_first_call: 标记是否是当前作用域/上下文中的第一次调用。
  //                static 确保它在多次调用间保持值。只初始化一次为 true。
  static bool is_first_call = true;
  // ------------------------------------

  auto now = std::chrono::steady_clock::now(); // 获取当前时间

  if (is_first_call) {
    // 如果是第一次调用 (is_first_call 为 true)
    is_first_call = false; // 将标记设为 false，以便下次调用时打印
                           // 首次调用不打印任何信息，只记录时间
  } else {
    // 如果不是第一次调用 (is_first_call 为 false)
    auto duration = now - last_timepoint; // 计算与上一次记录的时间差
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count(); // 转换为毫秒

    // 打印信息
    if (!message.empty()) {
      PLOGI << "[" << message << "] ";
    }
    PLOGI << "距离上个标记点耗时: " << ms << " ms" << std::endl;
  }

  // 无论是否首次调用，都必须更新 last_timepoint 为当前时间
  // 以便下一次调用时可以计算正确的间隔
  last_timepoint = now;
}

// --- LspServer 构造函数 ---
LspServer::LspServer()
    : docManager(),           // 初始化文档管理器
      analyzer(),             // 初始化分析器
      coutMtx(),              // 初始化 cout 互斥锁
                              // logMtx(),                 // logMtx 已移除
      nextRequestId(0),       // 初始化请求 ID 计数器
      running(true),          // 初始化运行状态为 true
      initialized(false),     // 初始化为未初始化状态
      shutdownReceived(false) // 初始化为未收到 shutdown 请求
{
  PLOGI << "LspServer 实例已创建。";
}

// --- 通信方法实现 ---

/**
 * @brief 从标准输入读取一个完整的 JSON-RPC 消息。
 * 处理 Content-Length 头部，然后读取指定长度的 JSON 内容。
 */
std::optional<json> LspServer::readMessage() {
  long length = -1;
  std::string buffer; // 用于读取头部行

  // 1. 读取 HTTP 风格的头部
  try {
    while (std::getline(std::cin, buffer)) {
      // 移除末尾的 \r (如果存在，以兼容 Windows 换行符)
      if (!buffer.empty() && buffer.back() == '\r') {
        buffer.pop_back();
      }

      PLOGV << "读取头部: [" << buffer << "]"; // 记录读取的头部行

      // 检查是否是 Content-Length 头部
      const std::string lengthPrefix = "Content-Length: ";
      if (buffer.rfind(lengthPrefix, 0) == 0) { // starts_with
        try {
          length = std::stol(buffer.substr(lengthPrefix.length()));
          PLOGD << "读取到 Content-Length: " << length;
        } catch (const std::invalid_argument &e) {
          PLOGE << "无效的 Content-Length 值: '" << buffer.substr(lengthPrefix.length()) << "'";
          length = -1; // 重置长度
        } catch (const std::out_of_range &e) {
          PLOGE << "Content-Length 值超出范围: '" << buffer.substr(lengthPrefix.length()) << "'";
          length = -1;
        }
      }
      // 检查是否是头部结束标记 (空行)
      else if (buffer.empty()) {
        break; // 头部结束，准备读取内容
      }
      // 可以选择性地处理其他头部，例如 Content-Type

      // 如果流结束或出错
      if (std::cin.eof() || std::cin.bad()) {
        PLOGW << "在读取头部时遇到 EOF 或流错误。";
        running = false; // 停止服务器
        return std::nullopt;
      }
    }
  } catch (const std::ios_base::failure &e) {
    PLOGE << "读取头部时发生 IO 异常: " << e.what() << " (code: " << e.code() << ")";
    running = false;
    return std::nullopt;
  } catch (const std::exception &e) {
    PLOGE << "读取头部时发生未知异常: " << e.what();
    running = false;
    return std::nullopt;
  }

  // 2. 检查 Content-Length 是否有效
  if (length <= 0) {
    if (std::cin.eof() || std::cin.bad()) { // 如果是因为流结束而没有读到长度
      PLOGI << "输入流已结束或出错，服务器停止。";
      running = false;
    } else {
      PLOGE << "无效或缺失 Content-Length，无法读取消息体。";
    }
    return std::nullopt;
  }

  // 3. 读取指定长度的消息体
  std::string content;
  try {
    content.resize(length);             // 调整字符串大小以容纳内容
    std::cin.read(&content[0], length); // 读取指定字节数

    // 检查是否成功读取了所需字节数
    if (!std::cin || std::cin.gcount() != length) {
      PLOGE << "读取消息体失败: 预期读取 " << length << " 字节，实际读取 " << std::cin.gcount()
            << " 字节。";
      running = false; // 严重的通信错误，停止服务器
      return std::nullopt;
    }
  } catch (const std::ios_base::failure &e) {
    PLOGE << "读取消息体时发生 IO 异常: " << e.what() << " (code: " << e.code() << ")";
    running = false;
    return std::nullopt;
  } catch (const std::exception &e) {
    PLOGE << "读取消息体时发生未知异常: " << e.what();
    running = false;
    return std::nullopt;
  }

  PLOGD << "接收到原始 JSON (" << length << " bytes): " << content;

  // 4. 解析 JSON 内容
  try {
    return json::parse(content); // 使用 nlohmann/json 解析
  } catch (json::parse_error &e) {
    PLOGE << "JSON 解析错误: " << e.what() << "\n原始内容:\n" << content;
    // 向客户端发送无效 JSON 的错误响应？这比较困难，因为我们不知道请求 ID
    // 通常只能记录错误并尝试继续读取下一条消息
    return std::nullopt;
  }
}

/**
 * @brief 将 JSON 消息写入标准输出，添加必要的头部。线程安全。
 */
void LspServer::writeMessage(const json &msg) {
  try {
    // 序列化 JSON，禁止抛出异常，将无效 UTF-8 替换
    std::string content = msg.dump(-1, ' ', false, json::error_handler_t::replace);
    PLOGD << "发送 JSON (" << content.length() << " bytes): " << content;

    // 使用互斥锁保护对 std::cout 的写入
    std::lock_guard<std::mutex> lock(coutMtx);
    std::cout << "Content-Length: " << content.length() << "\r\n"; // 写入头部
    std::cout << "\r\n";                                           // 头部结束的空行
    std::cout << content << std::flush;                            // 写入内容并刷新缓冲区
  } catch (const std::exception &e) {
    PLOGF << "写入消息时发生异常: " << e.what();
    // 写入失败可能是严重问题，可能需要停止服务器
    running = false;
  } catch (...) {
    PLOGF << "写入消息时发生未知异常。";
    running = false;
  }
}

/**
 * @brief 发送成功的 JSON-RPC 响应。
 */
void LspServer::writeResponse(const JsonRpcId &id, const json &result) {
  // id 不能是 nullptr 用于成功响应
  if (std::holds_alternative<std::nullptr_t>(id)) {
    PLOGE << "尝试为没有 ID 的请求发送成功响应!";
    return;
  }
  json response = {{"jsonrpc", "2.0"},
                   //            {"id", id}, // 不支持这样构造
                   {"result", result}};

  // 使用 std::visit 处理 id (错误响应的 id 可以是 null)
  std::visit(
      [&response](auto &&arg) {
        response["id"] = arg; // nlohmann/json 可以处理 string, int, nullptr_t
      },
      id);

  writeMessage(response);
}

/**
 * @brief 发送错误的 JSON-RPC 响应。
 */
void LspServer::writeErrorResponse(const JsonRpcId &id, int code, const std::string &message,
                                   const std::optional<json> &data) {
  JsonRpcError error_obj;
  error_obj.code = code;
  error_obj.message = message;
  error_obj.data = data; // nlohmann/json 会处理 optional

  json response = {
      {"jsonrpc", "2.0"},
      //            {"id", id}, //不支持这样构造
      {"error", error_obj} // nlohmann/json 会调用 JsonRpcError 的 to_json
  };

  // 使用 std::visit 处理 id (错误响应的 id 可以是 null)
  std::visit(
      [&response](auto &&arg) {
        response["id"] = arg; // nlohmann/json 可以处理 string, int, nullptr_t
      },
      id);

  writeMessage(response);
}

/**
 * @brief 发送 JSON-RPC 通知。
 */
void LspServer::writeNotification(const std::string &method, const json &params) {
  json notification = {
      {"jsonrpc", "2.0"}, {"method", method}
      // "params" 字段只有在 params 非 null 时才添加 (nlohmann/json 的对象构造)
  };
  // 只有当 params 不是 null 或显式定义了才添加
  if (!params.is_null()) {
    notification["params"] = params;
  }
  writeMessage(notification);
}

// --- 主消息处理/分发 ---

/**
 * @brief 处理接收到的单个 JSON 消息体。
 */
void LspServer::handleMessage(const json &msg) {
  // 检查消息基本结markTimePoint("======== 接收到任意消息 ========");
  if (!msg.is_object()) {
    PLOGE << "接收到的消息不是一个 JSON 对象。";
    return;
  }
  if (!msg.contains("jsonrpc") || msg.value("jsonrpc", "") != "2.0") {
    PLOGE << "消息缺少或包含无效的 'jsonrpc' 版本。";
    return;
  }

  // 判断是请求 (有 id) 还是通知 (无 id)
  if (msg.contains("id")) { // 这是一个请求或响应
    JsonRpcId id;
    const auto &id_json = msg.at("id");
    if (id_json.is_string())
      id = id_json.get<std::string>();
    else if (id_json.is_number_integer())
      id = id_json.get<int>();
    else if (id_json.is_null())
      id = nullptr; // id 为 null 的情况? (可能是错误响应)
    else {
      PLOGE << "无效的请求/响应 ID 类型。";
      return;
    }

    if (msg.contains("method")) { // 这是一个请求
      std::string method = msg.value("method", "");
      json params = msg.value("params", json(nullptr)); // 获取参数，若无则为 null
      PLOGI << "收到请求 (ID: "
            << (std::holds_alternative<int>(id) ? std::to_string(std::get<int>(id))
                                                : std::get<std::string>(id))
            << "): " << method;

      // 在 shutdown 后只接受 exit 通知
      if (shutdownReceived) {
        PLOGW << "收到请求 '" << method << "' 但服务器已关闭，将发送错误。";
        writeErrorResponse(id, -32600, "Server shutting down"); // Invalid Request
        return;
      }

      // 在 initialized 之前只处理 initialize 请求
      if (!initialized && method != "initialize") {
        PLOGW << "收到请求 '" << method << "' 但服务器尚未初始化，将发送错误。";
        writeErrorResponse(id, -32002, "Server not initialized"); // ServerNotInitialized
        return;
      }

      // --- 请求分发 ---
      if (method == "initialize")
        handleInitialize(id, params);
      else if (method == "shutdown")
        handleShutdown(id);
      else if (method == "textDocument/completion")
        handleCompletion(id, params);
      else if (method == "textDocument/hover")
        handleHover(id, params);
      else if (method == "textDocument/definition")
        handleDefinition(id, params);
      // ... 添加其他请求处理 ...
      else
        handleUnknown(method, id); // 处理未知方法

    } else if (msg.contains("result") || msg.contains("error")) { // 这是一个响应
      PLOGI << "收到响应 (ID: "
            << (std::holds_alternative<int>(id)
                    ? std::to_string(std::get<int>(id))
                    : (std::holds_alternative<std::string>(id) ? std::get<std::string>(id)
                                                               : "null"))
            << ")";
      // TODO: 如果服务器需要发送请求并处理响应，在这里添加处理逻辑
    } else {
      PLOGE << "无效的 JSON-RPC 消息 (有 ID 但无 method/result/error)。";
    }
  } else if (msg.contains("method")) { // 这是一个通知 (无 id)
    std::string method = msg.value("method", "");
    json params = msg.value("params", json(nullptr));
    PLOGI << "收到通知: " << method;

    // 在 shutdown 后只接受 exit 通知
    if (shutdownReceived && method != "exit") {
      PLOGW << "收到通知 '" << method << "' 但服务器已关闭，将忽略。";
      return;
    }
    // $ 开头的通知是协议预留的，通常不需要处理
    if (method.rfind("$/", 0) == 0) {
      PLOGD << "忽略协议预留通知: " << method;
      return;
    }

    // --- 通知分发 ---
    if (method == "initialized")
      handleInitialized(params);
    else if (method == "exit")
      handleExit();
    else if (method == "textDocument/didOpen")
      handleDidOpen(params);
    else if (method == "textDocument/didChange")
      handleDidChange(params);
    else if (method == "textDocument/didClose")
      handleDidClose(params);
    // ... 添加其他通知处理 ...
    else {
      handleUnknown(method, std::nullopt);
    } // 处理未知方法

  } else {
    PLOGE << "无效的 JSON-RPC 消息 (无 id 且无 method)。";
  }
}

// --- 其他辅助功能实现 ---

/**
 * @brief 为指定的文档发布诊断信息。
 */
void LspServer::publishDiagnostics(const Uri &uri) {
  PLOGD << "发布诊断信息 for " << uri.str();
  // 从分析器获取该 URI 的所有诊断信息
  std::vector<Diagnostic> diagnostics = analyzer.getDiagnostics(uri);

  PublishDiagnosticsParams params;
  params.uri = uri.str();
  params.diagnostics = std::move(diagnostics);

  // 可选：获取文档版本号并包含在通知中
  std::optional<DocumentState> docState = docManager.getDocument(uri);
  if (docState.has_value()) {
    params.version = docState.value().version;
  }

  // 发送 textDocument/publishDiagnostics 通知
  writeNotification("textDocument/publishDiagnostics", params);
}

/**
 * @brief 尝试将 JSON 安全地解析为指定的 C++ 类型 T。
 * (模板函数实现在头文件中或在使用处实例化)
 */
template <typename T>
std::optional<T> LspServer::tryParseParams(const json &params, const std::string &method,
                                           const std::optional<JsonRpcId> &id) {
  std::string paramsStr = params.dump();
  if (params.is_null()) { // 检查 params 是否为 null
    PLOGE << "方法 '" << method << "' 的参数为 null 或缺失。";
    if (id.has_value()) { // 如果是请求，发送错误响应
      writeErrorResponse(id.value(), -32602,
                         "Invalid params: parameters are null or missing for method " + method);
    }
    return std::nullopt;
  }
  try {
    return params.get<T>(); // 尝试转换
  } catch (const json::exception &e) {
    PLOGE << "解析方法 '" << method << "' 的参数时发生错误: " << e.what() << "\n参数 JSON:\n"
          << params.dump(2);
    if (id.has_value()) { // 如果是请求，发送错误响应
      writeErrorResponse(id.value(), -32602,
                         "Invalid params: " + std::string(e.what())); // Invalid Params
    }
    return std::nullopt; // 转换失败
  }
}

// --- 服务器主循环 ---

/**
 * @brief 运行 LSP 服务器的主消息循环。
 */
// void LspServer::run() {
//     PLOGI << "LSP 服务器主循环开始运行...";
//     running = true;
//     initialized = false;
//     shutdownReceived = false;
//
//     // 设置 cin 抛出异常以更好地处理 IO 错误
//     std::cin.exceptions(std::ios::badbit | std::ios::failbit);
//
//     while (running && std::cin.good()) {
//         PLOGV << "等待下一条消息...";
//         std::optional<json> msg = readMessage(); // 读取消息
//
//         if (msg.has_value()) {
//             // 处理消息
//             try {
//                 handleMessage(msg.value());
//             } catch (const std::exception& e) {
//                 PLOGF << "处理消息时发生未捕获的标准异常: " << e.what() << "\n消息内容:\n" <<
//                 msg.value().dump(2);
//                 // 可以考虑是否要因为处理单个消息的错误而停止整个服务器
//                 // running = false;
//             } catch (...) {
//                 PLOGF << "处理消息时发生未捕获的未知异常。" << "\n消息内容:\n" <<
//                 msg.value().dump(2);
//                 // running = false;
//             }
//         } else {
//             // readMessage 返回 nullopt 通常意味着流结束或发生严重读取错误
//             if (running) { // 如果 running 标志仍然为 true，说明是意外结束
//                 PLOGW << "readMessage 返回空，但服务器仍在运行状态，可能输入流已关闭。";
//                 running = false; // 停止服务器
//             }
//         }
//         // 可以添加短暂休眠以避免 CPU 空转 (如果需要)
//         // std::this_thread::sleep_for(std::chrono::milliseconds(10));
//     }
//
//     PLOGI << "LSP 服务器主循环结束。";
// }

//---------------------------------------------------------------------------
// LSP 消息处理函数实现
//---------------------------------------------------------------------------
// (这些本应在 lsp_handlers.cpp 中，但根据要求合并到此文件)

void LspServer::handleInitialize(const JsonRpcId &id, const json &params) {
  PLOGI << "处理 'initialize' 请求 (ID: "
        << (std::holds_alternative<int>(id) ? std::to_string(std::get<int>(id))
                                            : std::get<std::string>(id))
        << ")";

  // 解析参数 (简化版)
  auto parsedParams = tryParseParams<InitializeParams>(params, "initialize", id);
  if (!parsedParams) {
    return;
  } // 解析失败，错误响应已发送

  // 记录客户端信息（如果可用）
  if (parsedParams->processId.has_value()) {
    PLOGI << " -> 客户端 Process ID: " << parsedParams->processId.value();
  }
  if (parsedParams->rootUri.has_value()) {
    PLOGI << " -> 工作区根目录 URI: "
          << parsedParams->rootUri.value(); /* TODO: 转换为 Uri 并存储 */
  }

  // 构建服务器能力响应
  InitializeResult result;
  result.capabilities.textDocumentSync = TextDocumentSyncKind::Full; // 支持全量同步
  result.capabilities.hoverProvider = true;
  result.capabilities.definitionProvider = true;
  result.capabilities.completionProvider =
      json{{"resolveProvider", false}, {"triggerCharacters", {".", ":"}}}; // 声明支持补全

  // TODO: 根据实际实现的特性填充更多 capabilities

  writeResponse(id, result); // 发送响应
  PLOGI << "'initialize' 请求处理完毕。";
}

void LspServer::handleInitialized(const json &params) {
  PLOGI << "收到 'initialized' 通知。服务器现在可以发送非响应消息了。";
  initialized = true;

  // 可以在这里触发一些初始化后的操作，例如请求工作区配置、扫描项目文件等
}

void LspServer::handleShutdown(const JsonRpcId &id) {
  PLOGI << "收到 'shutdown' 请求 (ID: "
        << (std::holds_alternative<int>(id)
                ? std::to_string(std::get<int>(id))
                : (std::holds_alternative<std::string>(id) ? std::get<std::string>(id) : "null"))
        << ")";
  shutdownReceived = true; // 设置关闭标志
  // 根据规范，shutdown 请求需要返回 null result
  writeResponse(id, nullptr);
  PLOGI << "'shutdown' 请求处理完毕，准备退出。";
  // 注意：此时服务器不应退出，应等待 exit 通知
}

void LspServer::handleExit() {
  PLOGI << "收到 'exit' 通知。";
  if (shutdownReceived) {
    PLOGI << "服务器正常退出。";
    running = false; // 设置运行标志为 false，主循环将结束
                     // std::exit(0); // 可以选择立即退出，或者让主循环自然结束
  } else {
    PLOGE << "收到 'exit' 通知但未先收到 'shutdown' 请求，将强制退出。";
    running = false;
    // std::exit(1); // 可以选择用非零代码退出
  }
}

void LspServer::handleDidOpen(const json &params) {

  PLOGI << "处理 'textDocument/didOpen' 通知";
  auto parsedParams = tryParseParams<DidOpenTextDocumentParams>(params, "textDocument/didOpen");
  if (!parsedParams) {
    return;
  }

  const TextDocumentItem &docItem = parsedParams.value().textDocument;
  Uri uri(docItem.uri);
  if (!uri.isValid()) {
    PLOGE << " -> 无效的文档 URI: " << docItem.uri;
    return;
  }

  PLOGI << " -> 文档: " << uri.str() << ", 版本: " << docItem.version
        << ", 语言ID: " << docItem.languageId;

  // 更新文档管理器
  docManager.openDocument(uri, docItem.text, docItem.version);

  // 触发异步或同步分析
  // 为了简单，这里同步调用分析并发布诊断
  analyzer.analyzeDocument(uri, docItem.text, docItem.version);
  publishDiagnostics(uri); // 发布诊断信息
}

void LspServer::handleDidChange(const json &params) {
  PLOGI << "处理 'textDocument/didChange' 通知";
  markTimePoint("开始标记 didChange");
  auto parsedParams = tryParseParams<DidChangeTextDocumentParams>(params, "textDocument/didChange");
  if (!parsedParams) {
    return;
  }

  const VersionedTextDocumentIdentifier &docId = parsedParams.value().textDocument;
  Uri uri(docId.uri);
  if (!uri.isValid()) {
    PLOGE << " -> 无效的文档 URI: " << docId.uri;
    return;
  }

  long version = docId.version.value_or(0); // 获取版本号，如果为 null 则用 0
  PLOGI << " -> 文档: " << uri.str() << ", 新版本: " << version;

  // 由于我们只支持 Full sync，所以只关心最后一个 change event 的 text
  if (parsedParams.value().contentChanges.empty()) {
    PLOGW << " -> 收到 didChange 通知但 contentChanges 为空: " << uri.str();
    return; // 没有内容变化，不做操作
  }
  const std::string &newContent = parsedParams.value().contentChanges.back().text;
  PLOGV << " -> 新内容长度: " << newContent.length();

  // 更新文档管理器
  docManager.updateDocument(uri, newContent, version);

  // 触发分析并发布诊断
  analyzer.analyzeDocument(uri, newContent, version);
  publishDiagnostics(uri);
  markTimePoint("结束标记 didChange");
}

void LspServer::handleDidClose(const json &params) {
  PLOGI << "处理 'textDocument/didClose' 通知";
  auto parsedParams = tryParseParams<DidCloseTextDocumentParams>(params, "textDocument/didClose");
  if (!parsedParams) {
    return;
  }

  Uri uri(parsedParams.value().textDocument.uri);
  if (!uri.isValid()) {
    PLOGE << " -> 无效的文档 URI: " << parsedParams.value().textDocument.uri;
    return;
  }

  PLOGI << " -> 文档: " << uri.str();

  // 从文档管理器移除
  docManager.closeDocument(uri);
  // 清除分析缓存
  analyzer.clearAnalysis(uri);
  // 清除该文件的诊断信息 (发送空的诊断列表)
  PublishDiagnosticsParams clearParams;
  clearParams.uri = uri.str();
  clearParams.diagnostics = {};
  writeNotification("textDocument/publishDiagnostics", clearParams);
}

// 在 src/lsp_server.cpp 中

void LspServer::handleCompletion(const JsonRpcId &id, const json &params) {
  PLOGI << "处理 'textDocument/completion' 请求 (ID: "
        << (std::holds_alternative<int>(id)
                ? std::to_string(std::get<int>(id))
                : (std::holds_alternative<std::string>(id) ? std::get<std::string>(id) : "null"))
        << ")";

  // 1. 解析参数
  // CompletionParams 包含 TextDocumentPositionParams 和可选的 CompletionContext
  auto parsedParams = tryParseParams<CompletionParams>(params, "textDocument/completion", id);
  if (!parsedParams) {
    // tryParseParams 内部会在解析失败时发送错误响应
    return;
  }

  const CompletionParams &completionParams = parsedParams.value();
  Uri uri(completionParams.textDocument.uri);
  Position position = completionParams.position;
  // 获取 LSP 客户端提供的上下文信息
  std::optional<CompletionContext> lspCompletionContext = completionParams.context;

  if (!uri.isValid()) {
    PLOGE << " -> 无效的 URI 用于补全请求: " << completionParams.textDocument.uri;
    writeErrorResponse(id, JsonRpcErrorCode::InvalidParams,
                       "Invalid URI provided for completion request.");
    return;
  }

  // 2. 调用分析器的 getCompletions 方法
  PLOGD << " -> 调用 analyzer.getCompletions for " << uri.str() << " at L" << position.line << "C"
        << position.character;
  if (lspCompletionContext.has_value()) {
    PLOGD << " -> LSP CompletionContext: TriggerKind="
          << static_cast<int>(lspCompletionContext->triggerKind)
          << ", TriggerChar=" << lspCompletionContext->triggerCharacter.value_or("N/A");
  } else {
    PLOGD << " -> LSP CompletionContext: Not provided.";
  }

  std::optional<CompletionList> completionList =
      analyzer.getCompletions(uri, position,
                              docManager,          // 传递 DocumentManager
                              lspCompletionContext // 传递 LSP 提供的上下文
      );

  // 3. 发送响应
  if (completionList.has_value()) {
    PLOGI << " -> 准备发送 " << completionList.value().items.size()
          << " 个补全项。 Incomplete: " << completionList.value().isIncomplete;
    writeResponse(id, completionList.value());
  } else {
    // 根据 LSP 规范，如果无法提供补全，可以返回 null 或空列表
    PLOGI << " -> 未能生成补全列表，返回 null。";
    writeResponse(id, nullptr); // 或者 writeResponse(id, json::array());
  }
}

void LspServer::handleHover(const JsonRpcId &id, const json &params) {
  PLOGI << "处理 'textDocument/hover' 请求 (ID: "
        << (std::holds_alternative<int>(id) ? std::to_string(std::get<int>(id))
                                            : std::get<std::string>(id))
        << ")";
  auto parsedParams = tryParseParams<TextDocumentPositionParams>(params, "textDocument/hover", id);
  if (!parsedParams) {
    return;
  }

  Uri uri(parsedParams.value().textDocument.uri);
  Position position = parsedParams.value().position;
  if (!uri.isValid()) {
    PLOGE << " -> 无效 URI";
    writeErrorResponse(id, -32602, "Invalid URI");
    return;
  }

  std::optional<Hover> hoverInfo = analyzer.getHoverInfo(uri, position);

  if (hoverInfo.has_value()) {
    writeResponse(id, hoverInfo.value());
  } else {
    // 返回 null 表示没有悬停信息
    writeResponse(id, nullptr);
  }
}

void LspServer::handleDefinition(const JsonRpcId &id, const json &params) {
  PLOGI << "处理 'textDocument/definition' 请求 (ID: "
        << (std::holds_alternative<int>(id) ? std::to_string(std::get<int>(id))
                                            : std::get<std::string>(id))
        << ")";
  auto parsedParams =
      tryParseParams<TextDocumentPositionParams>(params, "textDocument/definition", id);
  if (!parsedParams) {
    return;
  }

  Uri uri(parsedParams.value().textDocument.uri);
  Position position = parsedParams.value().position;
  if (!uri.isValid()) {
    PLOGE << " -> 无效 URI";
    writeErrorResponse(id, -32602, "Invalid URI");
    return;
  }

  std::optional<Location> location = analyzer.findDefinition(uri, position);

  if (location.has_value()) {
    // LSP v3 定义返回值可以是 Location | Location[] | LocationLink[]
    // 简单起见，我们只返回单个 Location 或 null
    writeResponse(id, location.value());
  } else {
    // 返回 null 表示未找到定义
    writeResponse(id, nullptr);
  }
}

void LspServer::handleUnknown(const std::string &method, const std::optional<JsonRpcId> &id) {
  PLOGW << "收到未知或不支持的方法: " << method << (id.has_value() ? " (有 ID)" : " (无 ID)");
  if (id.has_value()) {
    // 如果是请求，回复 MethodNotFound 错误
    writeErrorResponse(id.value(), -32601, "Method not found: " + method);
  }
  // 如果是通知，则忽略
}