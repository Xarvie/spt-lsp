#ifndef LSP_SERVER_H
#define LSP_SERVER_H

#include "document_manager.h"   // 引入文档管理器
#include "protocol_types.h"     // 引入 LSP 协议类型 (包含 json.hpp)
#include "sptscript_analyzer.h" // 引入语言分析器
#include "uri.h"                // 引入 Uri 类

#include <atomic>   // 引入标准原子类型
#include <iostream> // 引入标准输入输出流
#include <mutex>    // 引入标准互斥量 (用于 cout)
#include <optional> // 引入标准可选类型
#include <string>   // 引入标准字符串
#include <variant>  // 引入标准变体类型 (用于 JsonRpcId)

// 使用 nlohmann/json 命名空间
using json = nlohmann::json;
// 使用我们定义的 JSON-RPC ID 类型
using JsonRpcId = std::variant<std::string, int, std::nullptr_t>;

// 使用 inline 建议编译器优化，减少函数调用开销
void markTimePoint(const std::string &message = "");

/**
 * @brief LSP 服务器主类。
 * 负责处理与客户端的通信、管理文档状态、协调代码分析，
 * 以及响应各种 LSP 请求和通知。
 */
class LspServer {
private:
  // --- 核心组件 ---
  DocumentManager docManager; // 文档管理器实例
  SptScriptAnalyzer analyzer; // 语言分析器实例

  // --- 状态与同步 ---
  std::mutex coutMtx;              // 保护对 std::cout (标准输出) 的并发访问
  std::atomic<long> nextRequestId; // 原子计数器，用于生成服务器发起的请求 ID (如果需要)
  std::atomic<bool> running;          // 控制服务器主循环是否运行
  std::atomic<bool> initialized;      // 标记客户端是否已发送 initialized 通知
  std::atomic<bool> shutdownReceived; // 标记是否收到了 shutdown 请求

  // --- 私有辅助方法：通信 ---

  /**
   * @brief 从标准输入流读取一个完整的 JSON-RPC 消息。
   * 会处理 Content-Length 头部信息。
   * @return 如果成功读取并解析消息，返回包含 JSON 内容的 std::optional<json>。
   * 如果遇到流错误、无效头部或 JSON 解析错误，则返回 std::nullopt。
   */
  std::optional<json> readMessage();

  /**
   * @brief 将一个 JSON 对象序列化为字符串，并作为 JSON-RPC 消息写入标准输出。
   * 会自动添加 Content-Length 头部和必需的 \r\n 分隔符。
   * 使用 coutMtx 保证写入操作的线程安全。
   * @param msg 要发送的 JSON 消息对象。
   */
  void writeMessage(const json &msg);

  /**
   * @brief 发送一个成功的 JSON-RPC 响应给客户端。
   * @param id 对应的原始请求 ID (string 或 int)。
   * @param result 响应的结果内容 (任何可序列化为 JSON 的值)。
   */
  void writeResponse(const JsonRpcId &id, const json &result);

  /**
   * @brief 发送一个错误的 JSON-RPC 响应给客户端。
   * @param id 对应的原始请求 ID (可以是 null，如果请求本身解析失败)。
   * @param code 符合 JSON-RPC 规范的错误代码。
   * @param message 描述错误的字符串。
   * @param data (可选) 附加的错误数据。
   */
  void writeErrorResponse(const JsonRpcId &id, int code, const std::string &message,
                          const std::optional<json> &data = std::nullopt);

  /**
   * @brief 发送一个 JSON-RPC 通知给客户端。
   * @param method 通知的名称 (例如 "textDocument/publishDiagnostics")。
   * @param params 通知的参数 (任何可序列化为 JSON 的值)。
   */
  void writeNotification(const std::string &method, const json &params);

  // --- 私有辅助方法：LSP 消息处理函数 ---
  // 这些函数的具体实现将在 lsp_server.cpp 文件中定义。

  /** @brief 处理来自客户端的 'initialize' 请求。*/
  void handleInitialize(const JsonRpcId &id, const json &params);
  /** @brief 处理来自客户端的 'initialized' 通知。*/
  void handleInitialized(const json &params);
  /** @brief 处理来自客户端的 'shutdown' 请求。*/
  void handleShutdown(const JsonRpcId &id);
  /** @brief 处理来自客户端的 'exit' 通知。*/
  void handleExit();

  /** @brief 处理 'textDocument/didOpen' 通知。*/
  void handleDidOpen(const json &params);
  /** @brief 处理 'textDocument/didChange' 通知。*/
  void handleDidChange(const json &params);
  /** @brief 处理 'textDocument/didClose' 通知。*/
  void handleDidClose(const json &params);

  /** @brief 处理 'textDocument/completion' 请求。*/
  void handleCompletion(const JsonRpcId &id, const json &params);
  /** @brief 处理 'textDocument/hover' 请求。*/
  void handleHover(const JsonRpcId &id, const json &params);
  /** @brief 处理 'textDocument/definition' 请求。*/
  void handleDefinition(const JsonRpcId &id, const json &params);

  /** @brief 处理服务器不认识或不支持的请求/通知方法。*/
  void handleUnknown(const std::string &method, const std::optional<JsonRpcId> &id);

  // --- 私有辅助方法：其他 ---
  /**
   * @brief 为指定的文档 URI 发布诊断信息。
   * 它会调用分析器获取最新的诊断信息，并发送 'textDocument/publishDiagnostics' 通知。
   * @param uri 需要发布诊断的文档 URI。
   */
  void publishDiagnostics(const Uri &uri);

  /**
   * @brief 尝试将 JSON 值安全地解析为指定的 C++ 类型 T。
   * @tparam T 目标 C++ 类型，必须提供对应的 from_json 函数。
   * @param params 可能包含参数的 JSON 值。
   * @param method 当前处理的方法名，用于日志记录。
   * @param id (可选) 如果解析失败且这是一个请求，用于发送错误响应。
   * @return 如果成功解析，返回包含 T 实例的 std::optional；否则返回
   * std::nullopt，并可能记录错误或发送错误响应。
   */
  template <typename T>
  std::optional<T> tryParseParams(const json &params, const std::string &method,
                                  const std::optional<JsonRpcId> &id = std::nullopt);

public:
  /**
   * @brief 构造 LspServer 对象。
   * 初始化状态标志和请求 ID 计数器。
   */
  LspServer();

  // 删除拷贝构造和拷贝赋值，LspServer 不应被拷贝
  LspServer(const LspServer &) = delete;
  LspServer &operator=(const LspServer &) = delete;

  /**
   * @brief 处理从客户端接收到的单个 JSON 消息体。
   * 判断消息是请求、响应还是通知，并将其分发给相应的处理函数。
   * @param messageBody 已从输入流成功解析的 JSON 对象。
   */
  void handleMessage(const json &messageBody);
};

#endif // LSP_SERVER_H