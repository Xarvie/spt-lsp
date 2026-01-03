#ifndef DOCUMENT_MANAGER_H
#define DOCUMENT_MANAGER_H

#include "uri.h"         // 引入我们定义的 Uri 类
#include <mutex>         // 引入 C++ 标准互斥量库 (用于线程安全)
#include <optional>      // 引入 C++ 标准可选类型 (用于安全地获取文档)
#include <string>        // 引入 C++ 标准字符串库
#include <unordered_map> // 引入 C++ 标准无序映射容器 (用于存储文档)
#include <vector>        // 引入 C++ 标准向量容器 (用于获取所有 URI)

/**
 * @brief 存储单个文档当前状态的结构体。
 */
struct DocumentState {
  std::string content; // 文档的当前完整文本内容
  long version = 0;    // 来自客户端的文档版本号

  // 可以在此结构体中添加缓存的分析结果 (例如 ParseResult 或 AnalysisResult 的共享指针)
  // 以优化性能，避免重复分析未更改的文档。
  // std::shared_ptr<ParseResult> cachedParseResult;
  // std::shared_ptr<AnalysisResult> cachedAnalysisResult;
};

/**
 * @brief 文档管理器类。
 * 负责在内存中跟踪客户端编辑器中打开的文档及其内容和版本。
 * 这个类的设计考虑了线程安全，内部使用互斥锁保护数据访问。
 */
class DocumentManager {
private:
  /**
   * @brief 使用文档的 Uri 作为键存储 DocumentState。
   * std::unordered_map 提供高效的平均查找时间。
   */
  std::unordered_map<Uri, DocumentState> documents;

  /**
   * @brief 互斥量，用于保护对 'documents' 映射的并发读写访问。
   */
  std::mutex mtx;

public:
  /**
   * @brief 默认构造函数。
   */
  DocumentManager() = default;

  /**
   * @brief 删除拷贝构造函数。
   * DocumentManager 包含互斥锁，通常不应被拷贝。
   */
  DocumentManager(const DocumentManager &) = delete;

  /**
   * @brief 删除拷贝赋值运算符。
   */
  DocumentManager &operator=(const DocumentManager &) = delete;

  // 可以根据需要显式定义移动构造和移动赋值 (如果默认的足够)
  // DocumentManager(DocumentManager&&) noexcept = default;
  // DocumentManager& operator=(DocumentManager&&) noexcept = default;

  /**
   * @brief 处理文档打开事件 (textDocument/didOpen)。
   * 将新文档添加到管理器中，或用新内容和版本覆盖已存在的文档。
   * @param uri 文档的唯一标识符 (Uri 对象)。
   * @param content 文档的初始完整内容。
   * @param version 文档的初始版本号。
   */
  void openDocument(const Uri &uri, const std::string &content, long version);

  /**
   * @brief 处理文档更改事件 (textDocument/didChange)。
   * 此实现假定客户端发送的是完整文本内容 (Full Sync)。
   * 更新指定 URI 文档的内容和版本号。
   * @param uri 文档的唯一标识符 (Uri 对象)。
   * @param content 文档更新后的完整内容。
   * @param version 文档更新后的版本号。
   */
  void updateDocument(const Uri &uri, const std::string &content, long version);

  /**
   * @brief 处理文档关闭事件 (textDocument/didClose)。
   * 从管理器中移除指定 URI 的文档状态和相关缓存（如果实现）。
   * @param uri 要关闭文档的唯一标识符 (Uri 对象)。
   */
  void closeDocument(const Uri &uri);

  /**
   * @brief 获取指定 URI 文档的当前状态（内容和版本）。
   * 这是一个线程安全的操作，内部使用锁保护访问。
   * @param uri 要获取状态的文档 URI。
   * @return 如果文档存在于管理器中，返回包含其 DocumentState 的 std::optional；
   * 如果文档不存在，返回 std::nullopt。
   */
  std::optional<DocumentState> getDocument(const Uri &uri);

  /**
   * @brief 获取当前管理器中所有已打开文档的 URI 列表。
   * 这是一个线程安全的操作。
   * @return 包含所有文档 Uri 的 std::vector。
   */
  std::vector<Uri> getAllDocumentUris();
};

#endif // DOCUMENT_MANAGER_H