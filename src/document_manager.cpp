#include "document_manager.h" // 包含 DocumentManager 的声明
#include <mutex>              // 引入 std::lock_guard, std::mutex
#include <optional>           // 引入 std::optional
#include <string>             // 引入 std::string
#include <utility>            // 引入 std::move
#include <vector>             // 引入 std::vector

// --- 引入 plog 日志库 ---
#include <plog/Log.h>

/**
 * @brief 处理文档打开事件 (textDocument/didOpen)。线程安全。
 * 将新文档添加到管理器中，或者用新内容和版本覆盖已存在的同名文档。
 * @param uri 文档的唯一标识符 (Uri 对象)。
 * @param content 文档的初始完整内容。
 * @param version 文档的初始版本号。
 */
void DocumentManager::openDocument(const Uri &uri, const std::string &content, long version) {
  // 使用 lock_guard 在当前作用域内自动管理互斥锁 mtx 的锁定和解锁
  std::lock_guard<std::mutex> lock(mtx);

  PLOGI << "打开/更新文档: " << uri.str() << " (版本: " << version << ")";

  // 创建新的文档状态对象
  DocumentState newState;
  newState.content = content;
  newState.version = version;

  // 使用 insert_or_assign 将新状态插入或赋给映射中的对应 URI
  // 它会返回一个 pair，包含指向元素的迭代器和表示是否为新插入的布尔值
  auto [it, inserted] = documents.insert_or_assign(uri, std::move(newState)); // 使用移动语义

  if (inserted) {
    PLOGD << "新文档 '" << uri.str() << "' 已添加到管理器。";
  } else {
    // 如果文档已存在，insert_or_assign 会覆盖旧值
    PLOGD << "现有文档 '" << uri.str() << "' 已在管理器中更新。";
  }
}

/**
 * @brief 处理文档更改事件 (textDocument/didChange)。线程安全。
 * 此实现假定客户端发送的是完整文本内容 (Full Sync)。
 * 更新指定 URI 文档的内容和版本号。
 * @param uri 文档的唯一标识符 (Uri 对象)。
 * @param content 文档更新后的完整内容。
 * @param version 文档更新后的版本号。
 */
void DocumentManager::updateDocument(const Uri &uri, const std::string &content, long version) {
  std::lock_guard<std::mutex> lock(mtx); // 锁定互斥量

  PLOGI << "更改文档: " << uri.str() << " (新版本: " << version << ")";

  // 查找文档是否存在于映射中
  auto it = documents.find(uri);
  if (it != documents.end()) {
    // 文档存在，更新其状态
    DocumentState &existingState = it->second; // 获取现有状态的引用

    // 可选的版本检查，用于检测可能的乱序通知
    if (version <= existingState.version && version != 0) { // version 0 有时用于特殊情况
      PLOGW << "接收到的文档版本 (" << version << ") 不高于当前存储的版本 ("
            << existingState.version << ")，URI: " << uri.str() << "。可能存在乱序通知。";
      // 尽管版本号旧，但仍应用更新以反映最新收到的内容
    }

    // 更新内容和版本号
    existingState.content = content;
    existingState.version = version;
    PLOGD << "文档 '" << uri.str() << "' 内容已更新。";

  } else {
    // 文档不在管理器中，这对于 didChange 来说通常是异常情况
    PLOGW << "尝试更新一个当前未被管理的文档: " << uri.str() << "。将按打开文档处理。";
    // 采取容错措施：像处理 openDocument 一样添加这个文档
    DocumentState newState;
    newState.content = content;
    newState.version = version;
    documents.insert({uri, std::move(newState)});
  }
}

/**
 * @brief 处理文档关闭事件 (textDocument/didClose)。线程安全。
 * 从管理器中移除指定 URI 的文档状态。
 * @param uri 要关闭文档的唯一标识符 (Uri 对象)。
 */
void DocumentManager::closeDocument(const Uri &uri) {
  std::lock_guard<std::mutex> lock(mtx); // 锁定互斥量

  PLOGI << "关闭文档: " << uri.str();

  // 使用 erase 从映射中移除指定键的元素
  // erase 返回被移除的元素数量 (对于 map/unordered_map，最多为 1)
  size_t erasedCount = documents.erase(uri);

  if (erasedCount > 0) {
    PLOGD << "文档 '" << uri.str() << "' 已从管理器移除。";
    // 注意: 在 LspServer 的对应 handler 中，调用此方法后，
    // 还应调用 analyzer.clearAnalysis(uri) 来清理分析缓存。
  } else {
    PLOGW << "尝试关闭一个当前未被管理的文档: " << uri.str();
  }
}

/**
 * @brief 获取指定 URI 文档的当前状态（内容和版本）。线程安全。
 * @param uri 要获取状态的文档 URI。
 * @return 如果文档存在于管理器中，返回包含其 DocumentState **副本** 的 std::optional；
 * 如果文档不存在，返回 std::nullopt。返回副本是为了避免将受锁保护的数据的引用暴露给外部。
 */
std::optional<DocumentState> DocumentManager::getDocument(const Uri &uri) {
  std::lock_guard<std::mutex> lock(mtx); // 锁定互斥量

  PLOGV << "查询文档状态: " << uri.str(); // 使用 Verbose 级别记录查询

  // 使用 find 在映射中查找 URI
  auto it = documents.find(uri);
  if (it != documents.end()) {
    // 找到文档，返回其状态的副本
    PLOGV << " -> 找到文档 '" << uri.str() << "' (版本: " << it->second.version << ")";
    return it->second; // 隐式构造 std::optional<DocumentState> 并返回值副本
  } else {
    // 未找到文档
    PLOGV << " -> 未找到文档 '" << uri.str() << "'";
    return std::nullopt; // 返回空的 optional
  }
}

/**
 * @brief 获取当前管理器中所有已打开文档的 URI 列表。线程安全。
 * @return 包含所有文档 Uri 的 std::vector。
 */
std::vector<Uri> DocumentManager::getAllDocumentUris() {
  std::lock_guard<std::mutex> lock(mtx); // 锁定互斥量

  std::vector<Uri> uris;
  uris.reserve(documents.size()); // 预分配向量大小，提高效率

  // 遍历映射，将每个键 (Uri 对象) 添加到结果向量中
  for (const auto &pair : documents) {
    uris.push_back(pair.first); // 添加 Uri 的副本
  }

  PLOGV << "获取所有已打开文档的 URI，总数: " << uris.size();
  return uris;
}