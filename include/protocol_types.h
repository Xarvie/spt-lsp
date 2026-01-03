#pragma once

#include <algorithm>   // 引入算法库 (用于 std::transform)
#include <map>         // 引入 C++ 标准映射容器
#include <optional>    // 引入 C++ 标准可选类型 (用于可选字段)
#include <stdexcept>   // 引入标准异常库
#include <string>      // 引入 C++ 标准字符串库
#include <type_traits> // 引入类型特性库 (用于 std::underlying_type)
#include <variant>     // 引入 C++ 标准变体类型 (用于 JsonRpcId)
#include <vector>      // 引入 C++ 标准向量容器

#include "json.hpp" // 引入 nlohmann/json 库
// #include "uri.h"          // 主要用 string

// 使用 nlohmann/json 命名空间简化代码
using json = nlohmann::json;

// ==========================================================================
// LSP/JSON-RPC 常量定义
// ==========================================================================

/**
 * @brief 定义常用的 LSP 方法名常量，防止拼写错误。
 */
namespace LspMethods {
// 生命周期管理 (Lifecycle)
constexpr const char *Initialize = "initialize";   // 初始化请求
constexpr const char *Initialized = "initialized"; // 初始化完成通知
constexpr const char *Shutdown = "shutdown";       // 关闭服务请求
constexpr const char *Exit = "exit";               // 退出服务通知

// 窗口相关 (Window) - 示例
constexpr const char *ShowMessage = "window/showMessage"; // 显示消息给用户
constexpr const char *LogMessage = "window/logMessage";   // 记录日志消息

// 工作区管理 (Workspace) - 示例
constexpr const char *DidChangeConfiguration = "workspace/didChangeConfiguration"; // 配置变更通知
constexpr const char *DidChangeWatchedFiles = "workspace/didChangeWatchedFiles"; // 监听文件变更通知
constexpr const char *WorkspaceFolders = "workspace/workspaceFolders"; // 获取工作区文件夹请求
constexpr const char *DidChangeWorkspaceFolders =
    "workspace/didChangeWorkspaceFolders"; // 工作区文件夹变更通知

// 文档同步 (Text Document Synchronization)
constexpr const char *DidOpen = "textDocument/didOpen";     // 文档已打开通知
constexpr const char *DidChange = "textDocument/didChange"; // 文档已更改通知
constexpr const char *DidClose = "textDocument/didClose";   // 文档已关闭通知
constexpr const char *DidSave = "textDocument/didSave";     // 文档已保存通知
constexpr const char *WillSave = "textDocument/willSave";   // 文档将要保存通知 (不常用)
constexpr const char *WillSaveWaitUntil =
    "textDocument/willSaveWaitUntil"; // 文档将要保存并等待编辑结果 (不常用)

// 诊断 (Diagnostics)
constexpr const char *PublishDiagnostics = "textDocument/publishDiagnostics"; // 发布诊断信息通知

// 语言特性 (Language Features)
constexpr const char *Completion = "textDocument/completion";               // 代码补全请求
constexpr const char *CompletionResolve = "completionItem/resolve";         // 补全项详情解析请求
constexpr const char *Hover = "textDocument/hover";                         // 悬停提示请求
constexpr const char *SignatureHelp = "textDocument/signatureHelp";         // 签名帮助请求
constexpr const char *Definition = "textDocument/definition";               // 跳转到定义请求
constexpr const char *TypeDefinition = "textDocument/typeDefinition";       // 跳转到类型定义请求
constexpr const char *Implementation = "textDocument/implementation";       // 跳转到实现请求
constexpr const char *References = "textDocument/references";               // 查找引用请求
constexpr const char *DocumentHighlight = "textDocument/documentHighlight"; // 文档高亮请求
constexpr const char *DocumentSymbol = "textDocument/documentSymbol";       // 文档符号请求
constexpr const char *WorkspaceSymbol = "workspace/symbol";                 // 工作区符号请求
constexpr const char *CodeAction = "textDocument/codeAction";               // 代码动作请求
constexpr const char *CodeLens = "textDocument/codeLens";                   // 代码镜头请求
constexpr const char *CodeLensResolve = "codeLens/resolve";                 // 代码镜头详情解析请求
constexpr const char *DocumentLink = "textDocument/documentLink";           // 文档链接请求
constexpr const char *DocumentLinkResolve = "documentLink/resolve";         // 文档链接详情解析请求
constexpr const char *DocumentColor = "textDocument/documentColor";         // 文档颜色请求 (不常用)
constexpr const char *ColorPresentation = "textDocument/colorPresentation"; // 颜色表示请求 (不常用)
constexpr const char *Formatting = "textDocument/formatting";               // 文档格式化请求
constexpr const char *RangeFormatting = "textDocument/rangeFormatting";     // 范围格式化请求
constexpr const char *OnTypeFormatting = "textDocument/onTypeFormatting";   // 输入时格式化请求
constexpr const char *Rename = "textDocument/rename";                       // 重命名请求
constexpr const char *PrepareRename = "textDocument/prepareRename";         // 准备重命名请求
constexpr const char *FoldingRange = "textDocument/foldingRange";           // 代码折叠范围请求
constexpr const char *SelectionRange = "textDocument/selectionRange";       // 选择范围请求
constexpr const char *SemanticTokensFull = "textDocument/semanticTokens/full"; // 完整语义令牌请求
constexpr const char *SemanticTokensFullDelta =
    "textDocument/semanticTokens/full/delta";                                    // 增量语义令牌请求
constexpr const char *SemanticTokensRange = "textDocument/semanticTokens/range"; // 范围语义令牌请求
constexpr const char *LinkedEditingRange =
    "textDocument/linkedEditingRange";                        // 链接编辑范围请求 (如 HTML 标签同步)
constexpr const char *InlayHint = "textDocument/inlayHint";   // 内嵌提示请求
constexpr const char *InlayHintResolve = "inlayHint/resolve"; // 内嵌提示详情解析请求
constexpr const char *CallHierarchyIncomingCalls = "callHierarchy/incomingCalls"; // 调用层级 (入)
constexpr const char *CallHierarchyOutgoingCalls = "callHierarchy/outgoingCalls"; // 调用层级 (出)
constexpr const char *PrepareCallHierarchy = "textDocument/prepareCallHierarchy"; // 准备调用层级
} // namespace LspMethods

/**
 * @brief 定义标准的 JSON-RPC 错误代码常量。
 */
namespace JsonRpcErrorCode {
// 由 JSON RPC 规范定义
constexpr const int ParseError = -32700;     // 解析错误: 服务器接收到无效的 JSON
constexpr const int InvalidRequest = -32600; // 无效请求: 发送的 JSON 不是一个有效的请求对象
constexpr const int MethodNotFound = -32601; // 方法未找到: 该方法不存在或无效
constexpr const int InvalidParams = -32602;  // 无效参数: 无效的方法参数
constexpr const int InternalError = -32603;  // 内部错误: JSON-RPC 框架内部错误

// -32000 到 -32099 是预留的服务器实现定义的错误代码
constexpr const int ServerErrorStart = -32099; // 服务器错误范围下限 (示例)
constexpr const int ServerNotInitialized =
    -32002;                                    // 服务器未初始化: 在收到初始化请求前接收到其他请求
constexpr const int UnknownErrorCode = -32001; // 未知错误代码
constexpr const int ServerErrorEnd = -32000;   // 服务器错误范围上限 (示例)

// LSP 协议定义的额外错误代码 (-32800 to -32899)
constexpr const int RequestFailed = -32803; // 请求失败 (通用 LSP 错误)
constexpr const int ServerCancelled =
    -32802; // 服务器取消 (例如服务器在完成前取消了长时间运行的操作)
constexpr const int ContentModified = -32801; // 内容已修改: 在操作期间文档内容被修改导致操作失败
constexpr const int RequestCancelledLSP = -32800; // 请求被取消 (LSP 特指, 通常是客户端取消)

} // namespace JsonRpcErrorCode

// ==========================================================================
// LSP 基础类型
// ==========================================================================

/**
 * @brief 表示文档中的位置 (行号和字符号，均为 0-based)。
 * LSP 协议规定行号和字符号都是从 0 开始计数。
 */
struct Position {
  long line = 0;      // 行号 (0-based)
  long character = 0; // 字符号 (0-based, 通常是 UTF-16 代码单元偏移量)
};

// 为 Position 定义 ADL (Argument-Dependent Lookup) 序列化函数
inline void to_json(json &j, const Position &p) { // NOLINT(misc-header-implementation)
  j = json{{"line", p.line}, {"character", p.character}};
}

inline void from_json(const json &j, Position &p) { // NOLINT(misc-header-implementation)
  j.at("line").get_to(p.line);
  j.at("character").get_to(p.character);
}

// 为 Position 提供内联比较操作符
inline bool operator==(const Position &lhs, const Position &rhs) {
  return lhs.line == rhs.line && lhs.character == rhs.character;
}

inline bool operator!=(const Position &lhs, const Position &rhs) { return !(lhs == rhs); }

inline bool operator<(const Position &lhs, const Position &rhs) {
  return lhs.line < rhs.line || (lhs.line == rhs.line && lhs.character < rhs.character);
}

inline bool operator<=(const Position &lhs, const Position &rhs) { return lhs < rhs || lhs == rhs; }

inline bool operator>(const Position &lhs, const Position &rhs) { return !(lhs <= rhs); }

inline bool operator>=(const Position &lhs, const Position &rhs) { return !(lhs < rhs); }

/**
 * @brief 表示文档中的一个范围，由起始和结束位置定义。
 * 范围通常包含起始位置，不包含结束位置。
 */
struct Range {
  Position start; // 范围的起始位置 (包含)
  Position end;   // 范围的结束位置 (通常不包含)
};

// 为 Range 定义 ADL 序列化函数
inline void to_json(json &j, const Range &r) { // NOLINT(misc-header-implementation)
  j = json{{"start", r.start}, {"end", r.end}};
}

inline void from_json(const json &j, Range &r) { // NOLINT(misc-header-implementation)
  j.at("start").get_to(r.start);
  j.at("end").get_to(r.end);
}

// 为 Range 提供内联比较操作符
inline bool operator==(const Range &lhs, const Range &rhs) {
  return lhs.start == rhs.start && lhs.end == rhs.end;
}

inline bool operator!=(const Range &lhs, const Range &rhs) { return !(lhs == rhs); }

inline bool operator<(const Range &lhs, const Range &rhs) {
  // 先比较起始位置，如果相同，再比较结束位置
  if (lhs.start < rhs.start) {
    return true;
  }
  if (rhs.start < lhs.start) {
    return false;
  }
  // 起始位置相同，比较结束位置
  return lhs.end < rhs.end;
}

/**
 * @brief 表示一个具体的位置链接，通常用于“跳转到定义”等功能。
 * 它将一个 URI 和该 URI 内的一个范围关联起来。
 */
struct Location {
  std::string uri; // 目标文档的 URI (协议中使用字符串)
  Range range;     // 目标文档内的范围
};

/**
 * @brief vscode新跳转结构体
 */
struct DefinitionLink {
  /**
   * 跳转前光标位置（按下ctrl高亮哪部分）
   */
  std::optional<Range> originSelectionRange;
  std::string targetUri; // 目标文档的 URI (协议中使用字符串)
  Range targetRange;     // 目标文档内的范围
  /**
   * 跳转后光标选中部分
   */
  Range targetSelectionRange;
};

// 为 Location 定义 ADL 序列化函数
inline void to_json(json &j, const DefinitionLink &l) { // NOLINT(misc-header-implementation)
  j = json{{"targetUri", l.targetUri},
           {"targetRange", l.targetRange},
           {"targetSelectionRange", l.targetSelectionRange}};
  if (l.originSelectionRange.has_value()) {
    j["originSelectionRange"] = *l.originSelectionRange;
  }
}

inline void from_json(const json &j, DefinitionLink &l) { // NOLINT(misc-header-implementation)
  j.at("targetUri").get_to(l.targetUri);
  j.at("targetRange").get_to(l.targetRange);
  j.at("targetSelectionRange").get_to(l.targetSelectionRange);
  if (j.count("originSelectionRange")) {
    Range range;
    j.at("originSelectionRange").get_to(range);
    l.originSelectionRange = range;
  }
}

/**
 * @brief 与 Location 类似，但还包含源头信息，用于链接编辑等。
 */
struct LocationLink {
  std::optional<Range> originSelectionRange; // 创建链接的原始文档中的范围 (可选)
  std::string targetUri;                     // 目标文档的 URI
  Range targetRange;                         // 目标文档中的完整范围
  Range targetSelectionRange;                // 目标文档中应选择/高亮的更精确范围
};

// 为 LocationLink 定义 ADL 序列化函数 (实现补充)
inline void to_json(json &j, const LocationLink &l) { // NOLINT(misc-header-implementation)
  j = json{{"targetUri", l.targetUri},
           {"targetRange", l.targetRange},
           {"targetSelectionRange", l.targetSelectionRange}};
  if (l.originSelectionRange.has_value()) {
    j["originSelectionRange"] = l.originSelectionRange.value();
  }
}

inline void from_json(const json &j, LocationLink &l) { // NOLINT(misc-header-implementation)
  j.at("targetUri").get_to(l.targetUri);
  j.at("targetRange").get_to(l.targetRange);
  j.at("targetSelectionRange").get_to(l.targetSelectionRange);
  if (j.contains("originSelectionRange") && !j.at("originSelectionRange").is_null()) {
    l.originSelectionRange = j.at("originSelectionRange").get<Range>();
  } else {
    l.originSelectionRange = std::nullopt;
  }
}

// --- 诊断信息 ---

/**
 * @brief 定义诊断信息的严重级别。
 * 数值与 LSP 规范一致。
 */
enum class DiagnosticSeverity {
  Error = 1,       // 错误: 通常由编译器/解释器报告
  Warning = 2,     // 警告: 可能的问题或建议
  Information = 3, // 信息: 提供额外信息
  Hint = 4         // 提示: 代码改进建议或风格提示
};

// 为 DiagnosticSeverity 枚举提供显式 ADL 转换函数
inline void to_json(json &j, const DiagnosticSeverity &e) { // NOLINT(misc-header-implementation)
  // LSP 规范使用数字表示严重级别
  j = static_cast<std::underlying_type_t<DiagnosticSeverity>>(e);
}

inline void from_json(const json &j, DiagnosticSeverity &e) { // NOLINT(misc-header-implementation)
  if (!j.is_number_integer()) {
    throw std::runtime_error("DiagnosticSeverity must be an integer");
  }
  auto value = j.get<std::underlying_type_t<DiagnosticSeverity>>();
  switch (value) {
  case 1:
    e = DiagnosticSeverity::Error;
    break;
  case 2:
    e = DiagnosticSeverity::Warning;
    break;
  case 3:
    e = DiagnosticSeverity::Information;
    break;
  case 4:
    e = DiagnosticSeverity::Hint;
    break;
  default:
    throw std::runtime_error("Invalid value for DiagnosticSeverity");
  }
}

/**
 * @brief 诊断信息标签，提供额外上下文 (例如 "不必要的代码")。
 */
enum class DiagnosticTag {
  Unnecessary = 1, // 未使用的代码
  Deprecated = 2   // 已废弃的代码
};

// 为 DiagnosticTag 提供显式 ADL 转换函数 (实现补充)
inline void to_json(json &j, const DiagnosticTag &e) { // NOLINT(misc-header-implementation)
  j = static_cast<std::underlying_type_t<DiagnosticTag>>(e);
}

inline void from_json(const json &j, DiagnosticTag &e) { // NOLINT(misc-header-implementation)
  if (!j.is_number_integer()) {
    throw std::runtime_error("DiagnosticTag must be an integer");
  }
  auto value = j.get<std::underlying_type_t<DiagnosticTag>>();
  switch (value) {
  case 1:
    e = DiagnosticTag::Unnecessary;
    break;
  case 2:
    e = DiagnosticTag::Deprecated;
    break;
  default:
    throw std::runtime_error("Invalid value for DiagnosticTag");
  }
}

/**
 * @brief 相关的诊断信息，指向问题相关的其他位置。
 */
struct DiagnosticRelatedInformation {
  DefinitionLink location; // 相关位置
  std::string message;     // 相关信息的消息文本
};

// 为 DiagnosticRelatedInformation 提供显式 ADL 转换函数
inline void to_json(json &j,
                    const DiagnosticRelatedInformation &ri) { // NOLINT(misc-header-implementation)
  j = json{{"location", ri.location}, {"message", ri.message}};
}

inline void from_json(const json &j,
                      DiagnosticRelatedInformation &ri) { // NOLINT(misc-header-implementation)
  j.at("location").get_to(ri.location);
  j.at("message").get_to(ri.message);
}

/**
 * @brief 表示一条具体的诊断信息，如编译错误或警告。
 */
struct Diagnostic {
  Range range;                                        // 诊断信息关联的代码范围 (必需)
  std::optional<DiagnosticSeverity> severity;         // 严重级别 (可选)
  std::optional<std::variant<int, std::string>> code; // 错误代码 (可选, 可以是数字或字符串)
  std::optional<std::string> codeDescription;     // 错误代码的描述 URI (可选, LSP期望{href:string})
  std::optional<std::string> source;              // 诊断来源，如 "编译器", "linter" (可选)
  std::string message;                            // 诊断消息文本 (必需)
  std::optional<std::vector<DiagnosticTag>> tags; // 标签列表 (可选, 如 Unnecessary, Deprecated)
  std::optional<std::vector<DiagnosticRelatedInformation>>
      relatedInformation;   // 相关信息列表 (可选)
  std::optional<json> data; // 附加数据，用于 CodeAction 等 (可选)
};

// 为 Diagnostic 提供显式的 ADL 转换函数
inline void to_json(json &j, const Diagnostic &d) {     // NOLINT(misc-header-implementation)
  j = json{{"range", d.range}, {"message", d.message}}; // 初始化包含必需字段
  if (d.severity.has_value()) {
    j["severity"] = d.severity.value();
  }
  if (d.code.has_value()) {
    std::visit([&j](auto &&arg) { j["code"] = arg; }, d.code.value());
  }
  if (d.codeDescription.has_value()) {
    j["codeDescription"] = json{{"href", d.codeDescription.value()}};
  } // LSP 要求是 {href: string}
  if (d.source.has_value()) {
    j["source"] = d.source.value();
  }
  if (d.tags.has_value() && !d.tags.value().empty()) {
    j["tags"] = d.tags.value();
  } // 确保 tags 有值且非空再序列化
  if (d.relatedInformation.has_value() && !d.relatedInformation.value().empty()) {
    j["relatedInformation"] = d.relatedInformation.value();
  } // 确保 relatedInformation 有值且非空
  if (d.data.has_value()) {
    j["data"] = d.data.value();
  }
}

inline void from_json(const json &j, Diagnostic &d) { // NOLINT(misc-header-implementation)
  j.at("range").get_to(d.range);
  j.at("message").get_to(d.message);

  if (j.contains("severity") && !j.at("severity").is_null()) {
    d.severity = j.at("severity").get<DiagnosticSeverity>();
  } else {
    d.severity = std::nullopt;
  }
  if (j.contains("code") && !j.at("code").is_null()) {
    const auto &code_json = j.at("code");
    if (code_json.is_number_integer())
      d.code = code_json.get<int>();
    else if (code_json.is_string())
      d.code = code_json.get<std::string>();
    else
      d.code = std::nullopt;
  } else {
    d.code = std::nullopt;
  }
  if (j.contains("codeDescription") && j.at("codeDescription").is_object() &&
      j.at("codeDescription").contains("href") && j.at("codeDescription").at("href").is_string()) {
    d.codeDescription = j.at("codeDescription").at("href").get<std::string>();
  } else {
    d.codeDescription = std::nullopt;
  }
  if (j.contains("source") && j.at("source").is_string()) {
    d.source = j.at("source").get<std::string>();
  } else {
    d.source = std::nullopt;
  }
  if (j.contains("tags") && j.at("tags").is_array()) {
    d.tags = j.at("tags").get<std::vector<DiagnosticTag>>();
  } else {
    d.tags = std::nullopt;
  }
  if (j.contains("relatedInformation") && j.at("relatedInformation").is_array()) {
    d.relatedInformation =
        j.at("relatedInformation").get<std::vector<DiagnosticRelatedInformation>>();
  } else {
    d.relatedInformation = std::nullopt;
  }
  if (j.contains("data") && !j.at("data").is_null()) {
    d.data = j.at("data");
  } else {
    d.data = std::nullopt;
  }
}

// --- 文档标识 ---

/**
 * @brief 唯一标识一个文本文档。
 */
struct TextDocumentIdentifier {
  std::string uri; // 文档的 URI (协议中使用字符串)
};

// 为 TextDocumentIdentifier 提供显式 ADL 转换函数
inline void to_json(json &j, const TextDocumentIdentifier &p) {
  j = json{{"uri", p.uri}};
} // NOLINT(misc-header-implementation)

inline void from_json(const json &j, TextDocumentIdentifier &p) {
  j.at("uri").get_to(p.uri);
} // NOLINT(misc-header-implementation)

/**
 * @brief 唯一标识一个特定版本的文本文档。
 */
struct VersionedTextDocumentIdentifier : TextDocumentIdentifier {
  // 版本号。对于未打开或已保存的文档，可以是 null。
  // 在 didChange 通知中必须提供递增的版本号。
  std::optional<long> version; // 文档的版本号 (可选, 规范中可以是 null 或 number)
};

// 为 VersionedTextDocumentIdentifier 提供显式 ADL 转换函数
inline void
to_json(json &j, const VersionedTextDocumentIdentifier &p) { // NOLINT(misc-header-implementation)
  // 先序列化基类
  to_json(j, static_cast<const TextDocumentIdentifier &>(p));
  // 处理可选字段 version
  if (p.version.has_value()) {
    j["version"] = p.version.value(); // 序列化为数字
  }
  // 如果 Optional 为空，省略该字段
}

inline void from_json(const json &j,
                      VersionedTextDocumentIdentifier &p) { // NOLINT(misc-header-implementation)
  // 先反序列化基类
  from_json(j, static_cast<TextDocumentIdentifier &>(p));
  // 安全地获取可选字段 version (可以是 null 或 number)
  if (j.contains("version")) {
    const auto &version_json = j.at("version");
    if (version_json.is_number_integer()) {
      p.version = version_json.get<long>();
    } else if (version_json.is_null()) {
      p.version = std::nullopt; // 将 JSON null 显式映射为 C++ nullopt
    } else {
      // 如果 version 存在但不是 null 或 number，视为无效 (或根据需要抛出异常)
      p.version = std::nullopt;
      // throw std::runtime_error("Version must be an integer or null.");
    }
  } else {
    p.version = std::nullopt; // 如果 JSON 中没有 version 字段，也视为 nullopt
  }
}

/**
 * @brief 描述一个文本文档项的完整信息，通常在打开文档时发送。
 */
struct TextDocumentItem {
  std::string uri;        // 文档 URI (必需)
  std::string languageId; // 语言 ID, 例如 "flx" (必需)
  long version;           // 文档的初始版本号 (必需)
  std::string text;       // 文档的完整内容 (必需)
};

// 为 TextDocumentItem 提供显式 ADL 转换函数
inline void to_json(json &j, const TextDocumentItem &p) {
  j = json{{"uri", p.uri}, {"languageId", p.languageId}, {"version", p.version}, {"text", p.text}};
} // NOLINT(misc-header-implementation)

inline void from_json(const json &j, TextDocumentItem &p) {
  j.at("uri").get_to(p.uri);
  j.at("languageId").get_to(p.languageId);
  j.at("version").get_to(p.version);
  j.at("text").get_to(p.text);
} // NOLINT(misc-header-implementation)

// ==========================================================================
// 文档同步参数
// ==========================================================================

/**
 * @brief textDocument/didOpen 通知的参数。
 */
struct DidOpenTextDocumentParams {
  TextDocumentItem textDocument; // 包含打开的文档信息 (必需)
};

// 为 DidOpenTextDocumentParams 提供显式 ADL 转换函数
inline void to_json(json &j, const DidOpenTextDocumentParams &p) {
  j = json{{"textDocument", p.textDocument}};
} // NOLINT(misc-header-implementation)

inline void from_json(const json &j, DidOpenTextDocumentParams &p) {
  j.at("textDocument").get_to(p.textDocument);
} // NOLINT(misc-header-implementation)

/**
 * @brief 文档内容改变事件。
 * LSP 支持增量更新 (带 range) 和全量更新 (不带 range)。
 * 此结构体定义仅包含 text，对应全量更新或增量更新的插入文本。
 */
struct TextDocumentContentChangeEvent {
  // std::optional<Range> range; // 增量更新：被替换的范围 (可选) - 在此文件中未定义
  // std::optional<long> rangeLength; // 增量更新：被替换范围的长度 (可选) - 在此文件中未定义
  std::string text; // 文档的新内容 (全量更新时是完整内容，增量更新时是插入的文本)
};

// 为 TextDocumentContentChangeEvent 提供显式 ADL 转换函数 (仅处理 text)
inline void to_json(json &j,
                    const TextDocumentContentChangeEvent &p) { // NOLINT(misc-header-implementation)
  // 注意: 一个完整的增量处理实现需要序列化 range 和 rangeLength (如果存在)
  j = json{{"text", p.text}};
  // if (p.range.has_value()) j["range"] = p.range.value();
  // if (p.rangeLength.has_value()) j["rangeLength"] = p.rangeLength.value();
}

inline void from_json(const json &j,
                      TextDocumentContentChangeEvent &p) { // NOLINT(misc-header-implementation)
  // 注意: 一个完整的增量处理实现需要解析 range 和 rangeLength (如果存在)
  j.at("text").get_to(p.text);
  // if (j.contains("range")) p.range = j.at("range").get<Range>();
  // if (j.contains("rangeLength")) p.rangeLength = j.at("rangeLength").get<long>();
}

/**
 * @brief textDocument/didChange 通知的参数。
 */
struct DidChangeTextDocumentParams {
  VersionedTextDocumentIdentifier textDocument;               // 包含变化后文档的新版本号 (必需)
  std::vector<TextDocumentContentChangeEvent> contentChanges; // 变化事件列表 (必需)
};

// 为 DidChangeTextDocumentParams 提供显式 ADL 转换函数
inline void to_json(json &j,
                    const DidChangeTextDocumentParams &p) { // NOLINT(misc-header-implementation)
  j = json{{"textDocument", p.textDocument}, {"contentChanges", p.contentChanges}};
}

inline void from_json(const json &j,
                      DidChangeTextDocumentParams &p) { // NOLINT(misc-header-implementation)
  j.at("textDocument").get_to(p.textDocument);
  j.at("contentChanges").get_to(p.contentChanges);
}

/**
 * @brief 定义文档保存的原因。
 */
enum class TextDocumentSaveReason {
  Manual = 1,     // 用户显式保存 (例如 Ctrl+S)
  AfterDelay = 2, // 自动保存 (延迟后)
  FocusOut = 3    // 失去焦点时自动保存
};

// 为 TextDocumentSaveReason 提供显式 ADL 转换函数
inline void to_json(json &j,
                    const TextDocumentSaveReason &e) { // NOLINT(misc-header-implementation)
  j = static_cast<std::underlying_type_t<TextDocumentSaveReason>>(e);
}

inline void from_json(const json &j,
                      TextDocumentSaveReason &e) { // NOLINT(misc-header-implementation)
  if (!j.is_number_integer()) {
    throw std::runtime_error("TextDocumentSaveReason must be an integer");
  }
  auto value = j.get<std::underlying_type_t<TextDocumentSaveReason>>();
  switch (value) {
  case 1:
    e = TextDocumentSaveReason::Manual;
    break;
  case 2:
    e = TextDocumentSaveReason::AfterDelay;
    break;
  case 3:
    e = TextDocumentSaveReason::FocusOut;
    break;
  default:
    throw std::runtime_error("Invalid value for TextDocumentSaveReason");
  }
}

/**
 * @brief textDocument/didSave 通知的参数。
 */
struct DidSaveTextDocumentParams {
  TextDocumentIdentifier textDocument; // 包含保存的文档标识 (必需)
  std::optional<std::string>
      text; // 保存时的完整文本内容 (可选, 仅当客户端能力 includeText=true 时发送)
};

// 为 DidSaveTextDocumentParams 提供显式 ADL 转换函数
inline void to_json(json &j,
                    const DidSaveTextDocumentParams &p) { // NOLINT(misc-header-implementation)
  j = json{{"textDocument", p.textDocument}};
  if (p.text.has_value()) {
    j["text"] = p.text.value();
  }
}

inline void from_json(const json &j,
                      DidSaveTextDocumentParams &p) { // NOLINT(misc-header-implementation)
  j.at("textDocument").get_to(p.textDocument);
  if (j.contains("text") && j.at("text").is_string()) {
    p.text = j.at("text").get<std::string>();
  } else {
    p.text = std::nullopt;
  }
}

/**
 * @brief textDocument/didClose 通知的参数。
 */
struct DidCloseTextDocumentParams {
  TextDocumentIdentifier textDocument; // 包含关闭的文档标识 (必需)
};

// 为 DidCloseTextDocumentParams 提供显式 ADL 转换函数
inline void to_json(json &j, const DidCloseTextDocumentParams &p) {
  j = json{{"textDocument", p.textDocument}};
} // NOLINT(misc-header-implementation)

inline void from_json(const json &j, DidCloseTextDocumentParams &p) {
  j.at("textDocument").get_to(p.textDocument);
} // NOLINT(misc-header-implementation)

/**
 * @brief textDocument/publishDiagnostics 通知的参数。
 */
struct PublishDiagnosticsParams {
  std::string uri;                     // 关联的文档 URI (必需)
  std::optional<long> version;         // 关联的文档版本 (可选, 用于避免旧诊断覆盖新诊断)
  std::vector<Diagnostic> diagnostics; // 诊断信息列表 (必需, 空列表表示清除诊断)
};

// 为 PublishDiagnosticsParams 提供显式 ADL 转换函数
inline void to_json(json &j,
                    const PublishDiagnosticsParams &p) {    // NOLINT(misc-header-implementation)
  j = json{{"uri", p.uri}, {"diagnostics", p.diagnostics}}; // 初始化必需字段
  if (p.version.has_value()) {
    j["version"] = p.version.value();
  }
}

inline void from_json(const json &j,
                      PublishDiagnosticsParams &p) { // NOLINT(misc-header-implementation)
  j.at("uri").get_to(p.uri);
  j.at("diagnostics").get_to(p.diagnostics);
  if (j.contains("version") && !j.at("version").is_null() && j.at("version").is_number_integer()) {
    p.version = j.at("version").get<long>();
  } else {
    p.version = std::nullopt;
  }
}

// ==========================================================================
// 初始化 (补充和修复)
// ==========================================================================

/**
 * @brief 客户端有关工作区编辑的能力。
 */
struct WorkspaceEditClientCapabilities {
  std::optional<bool> documentChanges; // 客户端是否支持 documentChanges 字段 (用于跨文件编辑)
  // 这里可以根据需要添加其他能力字段，如 resourceOperations, failureHandling
};

// 为 WorkspaceEditClientCapabilities 提供显式 ADL 转换函数 (实现补充)
inline void
to_json(json &j, const WorkspaceEditClientCapabilities &p) { // NOLINT(misc-header-implementation)
  j = json::object();                                        // 初始化为空对象
  if (p.documentChanges.has_value()) {
    j["documentChanges"] = p.documentChanges.value();
  }
  // 在此添加其他字段的序列化
}

inline void from_json(const json &j,
                      WorkspaceEditClientCapabilities &p) { // NOLINT(misc-header-implementation)
  if (j.contains("documentChanges") && j.at("documentChanges").is_boolean()) {
    p.documentChanges = j.at("documentChanges").get<bool>();
  } else {
    p.documentChanges = std::nullopt;
  }
  // 在此添加其他字段的反序列化
}

/**
 * @brief 客户端有关工作区的能力 (简化)。
 * 注意：原始文件中部分字段的序列化方式（如 didChangeConfiguration）被简化，
 * 以直接反映结构体中定义的 bool 类型，而非更复杂的 ClientCapabilities 结构。
 */
struct WorkspaceClientCapabilities {
  std::optional<bool> applyEdit; // 客户端是否支持 workspace/applyEdit 请求
  std::optional<WorkspaceEditClientCapabilities> workspaceEdit; // 有关 workspaceEdit 的详细能力
  std::optional<bool> didChangeConfiguration; // 是否支持接收配置变更通知 (简化表示)
  std::optional<bool> didChangeWatchedFiles;  // 是否支持接收监听文件变更通知 (简化表示)
  std::optional<bool> symbol;                 // 是否支持 workspace/symbol 请求 (简化表示)
  std::optional<bool> workspaceFolders;       // 是否支持工作区文件夹
  // 这里可以根据需要添加其他工作区能力字段
};

// 为 WorkspaceClientCapabilities 提供 ADL 序列化函数 (实现补充)
inline void to_json(json &j,
                    const WorkspaceClientCapabilities &p) { // NOLINT(misc-header-implementation)
  j = json::object();                                       // 初始化为空对象
  if (p.applyEdit.has_value())
    j["applyEdit"] = p.applyEdit.value();
  if (p.workspaceEdit.has_value())
    j["workspaceEdit"] = p.workspaceEdit.value();
  if (p.didChangeConfiguration.has_value())
    j["didChangeConfiguration"] = json{
        {"dynamicRegistration", p.didChangeConfiguration.value()}}; // 保持与原文件一致的示例包装
  if (p.didChangeWatchedFiles.has_value())
    j["didChangeWatchedFiles"] = json{
        {"dynamicRegistration", p.didChangeWatchedFiles.value()}}; // 保持与原文件一致的示例包装
  if (p.symbol.has_value())
    j["symbol"] = json{{"dynamicRegistration", p.symbol.value()}}; // 保持与原文件一致的示例包装
  if (p.workspaceFolders.has_value())
    j["workspaceFolders"] = p.workspaceFolders.value();
  // 添加其他字段的序列化...
}

inline void from_json(const json &j,
                      WorkspaceClientCapabilities &p) { // NOLINT(misc-header-implementation)
  if (j.contains("applyEdit") && j.at("applyEdit").is_boolean())
    p.applyEdit = j.at("applyEdit").get<bool>();
  else
    p.applyEdit = std::nullopt;
  if (j.contains("workspaceEdit"))
    p.workspaceEdit = j.at("workspaceEdit").get<WorkspaceEditClientCapabilities>();
  else
    p.workspaceEdit = std::nullopt;
  // 反序列化时简化处理，只检查字段是否存在以判断大致支持情况
  if (j.contains("didChangeConfiguration"))
    p.didChangeConfiguration = true;
  else
    p.didChangeConfiguration = std::nullopt; // 简化判断
  if (j.contains("didChangeWatchedFiles"))
    p.didChangeWatchedFiles = true;
  else
    p.didChangeWatchedFiles = std::nullopt; // 简化判断
  if (j.contains("symbol"))
    p.symbol = true;
  else
    p.symbol = std::nullopt; // 简化判断
  if (j.contains("workspaceFolders") && j.at("workspaceFolders").is_boolean())
    p.workspaceFolders = j.at("workspaceFolders").get<bool>();
  else
    p.workspaceFolders = std::nullopt;
  // 添加其他字段的反序列化...
}

/**
 * @brief 客户端有关文本文档的能力 (简化)。
 * 使用 std::optional<json> 来表示各种特性，允许客户端发送详细配置。
 */
struct TextDocumentClientCapabilities {
  // 同步能力通常在此定义，但更常用的是根级别的 textDocumentSync
  // std::optional<SynchronizationCapabilities> synchronization;

  // 主要语言特性的客户端支持情况 (用 json 简化)
  std::optional<json> completion;         // 补全能力的详细选项
  std::optional<json> hover;              // 悬停能力的详细选项
  std::optional<json> signatureHelp;      // 签名帮助能力的详细选项
  std::optional<json> definition;         // 跳转定义能力的详细选项
  std::optional<json> references;         // 查找引用能力的详细选项
  std::optional<json> documentSymbol;     // 文档符号能力的详细选项
  std::optional<json> formatting;         // 格式化能力的详细选项
  std::optional<json> publishDiagnostics; // 诊断能力的详细选项
  // 这里可以添加其他特性如 codeAction, rename 等的 json 定义
};

// 为 TextDocumentClientCapabilities 提供 ADL 序列化函数 (实现补充)
inline void to_json(json &j,
                    const TextDocumentClientCapabilities &p) { // NOLINT(misc-header-implementation)
  j = json::object();
  if (p.completion.has_value())
    j["completion"] = p.completion.value();
  if (p.hover.has_value())
    j["hover"] = p.hover.value();
  if (p.signatureHelp.has_value())
    j["signatureHelp"] = p.signatureHelp.value();
  if (p.definition.has_value())
    j["definition"] = p.definition.value();
  if (p.references.has_value())
    j["references"] = p.references.value();
  if (p.documentSymbol.has_value())
    j["documentSymbol"] = p.documentSymbol.value();
  if (p.formatting.has_value())
    j["formatting"] = p.formatting.value();
  if (p.publishDiagnostics.has_value())
    j["publishDiagnostics"] = p.publishDiagnostics.value();
  // 添加其他字段的序列化...
}

inline void from_json(const json &j,
                      TextDocumentClientCapabilities &p) { // NOLINT(misc-header-implementation)
  if (j.contains("completion") && !j.at("completion").is_null())
    p.completion = j.at("completion");
  else
    p.completion = std::nullopt;
  if (j.contains("hover") && !j.at("hover").is_null())
    p.hover = j.at("hover");
  else
    p.hover = std::nullopt;
  if (j.contains("signatureHelp") && !j.at("signatureHelp").is_null())
    p.signatureHelp = j.at("signatureHelp");
  else
    p.signatureHelp = std::nullopt;
  if (j.contains("definition") && !j.at("definition").is_null())
    p.definition = j.at("definition");
  else
    p.definition = std::nullopt;
  if (j.contains("references") && !j.at("references").is_null())
    p.references = j.at("references");
  else
    p.references = std::nullopt;
  if (j.contains("documentSymbol") && !j.at("documentSymbol").is_null())
    p.documentSymbol = j.at("documentSymbol");
  else
    p.documentSymbol = std::nullopt;
  if (j.contains("formatting") && !j.at("formatting").is_null())
    p.formatting = j.at("formatting");
  else
    p.formatting = std::nullopt;
  if (j.contains("publishDiagnostics") && !j.at("publishDiagnostics").is_null())
    p.publishDiagnostics = j.at("publishDiagnostics");
  else
    p.publishDiagnostics = std::nullopt;
  // 添加其他字段的反序列化...
}

/**
 * @brief 客户端的总能力集合 (简化)。
 */
struct ClientCapabilities {
  std::optional<WorkspaceClientCapabilities> workspace;       // 工作区相关能力
  std::optional<TextDocumentClientCapabilities> textDocument; // 文档相关能力
  std::optional<json> window;                                 // 窗口相关能力 (用 json 简化)
  std::optional<json> general;                                // 通用能力 (用 json 简化)
  std::optional<json> experimental;                           // 实验性能力 (用 json 简化)
};

// 为 ClientCapabilities 提供 ADL 序列化函数 (实现补充)
inline void to_json(json &j, const ClientCapabilities &p) { // NOLINT(misc-header-implementation)
  j = json::object();
  if (p.workspace.has_value())
    j["workspace"] = p.workspace.value();
  if (p.textDocument.has_value())
    j["textDocument"] = p.textDocument.value();
  if (p.window.has_value())
    j["window"] = p.window.value();
  if (p.general.has_value())
    j["general"] = p.general.value();
  if (p.experimental.has_value())
    j["experimental"] = p.experimental.value();
}

inline void from_json(const json &j, ClientCapabilities &p) { // NOLINT(misc-header-implementation)
  if (j.contains("workspace"))
    p.workspace = j.at("workspace").get<WorkspaceClientCapabilities>();
  else
    p.workspace = std::nullopt;
  if (j.contains("textDocument"))
    p.textDocument = j.at("textDocument").get<TextDocumentClientCapabilities>();
  else
    p.textDocument = std::nullopt;
  if (j.contains("window") && !j.at("window").is_null())
    p.window = j.at("window");
  else
    p.window = std::nullopt;
  if (j.contains("general") && !j.at("general").is_null())
    p.general = j.at("general");
  else
    p.general = std::nullopt;
  if (j.contains("experimental") && !j.at("experimental").is_null())
    p.experimental = j.at("experimental");
  else
    p.experimental = std::nullopt;
}

/**
 * @brief initialize 请求的参数 (补充了 ClientCapabilities)。
 */
struct InitializeParams {
  std::optional<long> processId; // 发起请求的客户端进程 ID (可选, 可以为 null)
  // clientName, clientVersion 已废弃, 使用 clientInfo
  std::optional<std::string> rootPath; // 工作区根路径 (已废弃, 使用 rootUri 或 workspaceFolders)
  std::optional<std::string> rootUri;  // 工作区根目录 URI (可选, 可以为 null)
  ClientCapabilities capabilities;     // 客户端声明的能力 (必需)
  std::optional<json> initializationOptions; // 客户端传递给服务器的初始化选项 (可选)
  std::optional<std::string> trace;          // LSP 追踪设置 ("off", "messages", "verbose") (可选)
  std::optional<std::vector<json>> workspaceFolders; // 打开的工作区文件夹列表 (可选, 推荐使用)
  std::optional<json> clientInfo; // 客户端信息 {name: string, version?: string} (可选)
};

// 为 InitializeParams 提供显式 ADL 转换函数 (实现补充)
inline void to_json(json &j, const InitializeParams &p) { // NOLINT(misc-header-implementation)
  j = json{{"capabilities", p.capabilities}};             // capabilities 是必需的
  if (p.processId.has_value())
    j["processId"] = p.processId.value();
  else
    j["processId"] = nullptr; // 允许为 null
  if (p.clientInfo.has_value())
    j["clientInfo"] = p.clientInfo.value();
  if (p.rootPath.has_value())
    j["rootPath"] = p.rootPath.value(); // Deprecated
  if (p.rootUri.has_value())
    j["rootUri"] = p.rootUri.value();
  else
    j["rootUri"] = nullptr; // 允许为 null
  if (p.initializationOptions.has_value())
    j["initializationOptions"] = p.initializationOptions.value();
  if (p.trace.has_value())
    j["trace"] = p.trace.value();
  if (p.workspaceFolders.has_value())
    j["workspaceFolders"] = p.workspaceFolders.value();
}

inline void from_json(const json &j, InitializeParams &p) { // NOLINT(misc-header-implementation)
  j.at("capabilities").get_to(p.capabilities);              // capabilities 是必需的

  if (j.contains("processId") && !j.at("processId").is_null())
    p.processId = j.at("processId").get<long>();
  else
    p.processId = std::nullopt;
  if (j.contains("clientInfo") && !j.at("clientInfo").is_null())
    p.clientInfo = j.at("clientInfo");
  else
    p.clientInfo = std::nullopt;
  if (j.contains("rootPath") && j.at("rootPath").is_string())
    p.rootPath = j.at("rootPath").get<std::string>();
  else
    p.rootPath = std::nullopt; // Deprecated
  if (j.contains("rootUri") && !j.at("rootUri").is_null() && j.at("rootUri").is_string())
    p.rootUri = j.at("rootUri").get<std::string>();
  else
    p.rootUri = std::nullopt;
  if (j.contains("initializationOptions") && !j.at("initializationOptions").is_null())
    p.initializationOptions = j.at("initializationOptions");
  else
    p.initializationOptions = std::nullopt;
  if (j.contains("trace") && j.at("trace").is_string())
    p.trace = j.at("trace").get<std::string>();
  else
    p.trace = std::nullopt;
  if (j.contains("workspaceFolders") && j.at("workspaceFolders").is_array())
    p.workspaceFolders = j.at("workspaceFolders").get<std::vector<json>>();
  else
    p.workspaceFolders = std::nullopt;
}

/**
 * @brief 定义客户端期望的文档同步方式。
 */
enum class TextDocumentSyncKind {
  None = 0,       // 不进行同步
  Full = 1,       // 文档改变时发送完整内容
  Incremental = 2 // 文档改变时发送增量变化
};

// 为 TextDocumentSyncKind 提供显式 ADL 转换函数
inline void to_json(json &j, const TextDocumentSyncKind &e) { // NOLINT(misc-header-implementation)
  j = static_cast<std::underlying_type_t<TextDocumentSyncKind>>(e);
}

inline void from_json(const json &j,
                      TextDocumentSyncKind &e) { // NOLINT(misc-header-implementation)
  if (!j.is_number_integer()) {
    throw std::runtime_error("TextDocumentSyncKind must be an integer");
  }
  auto value = j.get<std::underlying_type_t<TextDocumentSyncKind>>();
  switch (value) {
  case 0:
    e = TextDocumentSyncKind::None;
    break;
  case 1:
    e = TextDocumentSyncKind::Full;
    break;
  case 2:
    e = TextDocumentSyncKind::Incremental;
    break;
  default:
    throw std::runtime_error("Invalid value for TextDocumentSyncKind");
  }
}

/**
 * @brief 文档同步选项。
 */
struct TextDocumentSyncOptions {
  std::optional<bool> openClose;              // 是否发送 didOpen/didClose 通知
  std::optional<TextDocumentSyncKind> change; // 文档内容改变时的同步方式
  // std::optional<bool> willSave; // 是否发送 willSave 通知 - 在此文件中未定义
  // std::optional<bool> willSaveWaitUntil; // 是否发送 willSaveWaitUntil 请求 - 在此文件中未定义
  // std::optional<SaveOptions> save; // didSave 通知相关的选项 - 在此文件中未定义
};

// 为 TextDocumentSyncOptions 提供 ADL 序列化函数
inline void to_json(json &j,
                    const TextDocumentSyncOptions &p) { // NOLINT(misc-header-implementation)
  j = json::object();
  if (p.openClose.has_value())
    j["openClose"] = p.openClose.value();
  if (p.change.has_value())
    j["change"] = p.change.value();
  // 添加其他字段的序列化...
}

inline void from_json(const json &j,
                      TextDocumentSyncOptions &p) { // NOLINT(misc-header-implementation)
  if (j.contains("openClose") && j.at("openClose").is_boolean())
    p.openClose = j.at("openClose").get<bool>();
  else
    p.openClose = std::nullopt;
  if (j.contains("change") && !j.at("change").is_null())
    p.change = j.at("change").get<TextDocumentSyncKind>();
  else
    p.change = std::nullopt;
  // 添加其他字段的反序列化...
}

/**
 * @brief 代码补全选项。
 */
struct CompletionOptions {
  std::optional<std::vector<std::string>> triggerCharacters;   // 触发补全的字符
  std::optional<std::vector<std::string>> allCommitCharacters; // 自动提交补全的字符 (建议使用)
  std::optional<bool> resolveProvider; // 是否支持 completionItem/resolve 请求来获取额外信息
};

// 为 CompletionOptions 提供 ADL 序列化函数
inline void to_json(json &j, const CompletionOptions &p) { // NOLINT(misc-header-implementation)
  j = json::object();
  if (p.triggerCharacters.has_value())
    j["triggerCharacters"] = p.triggerCharacters.value();
  if (p.allCommitCharacters.has_value())
    j["allCommitCharacters"] = p.allCommitCharacters.value();
  if (p.resolveProvider.has_value())
    j["resolveProvider"] = p.resolveProvider.value();
}

inline void from_json(const json &j, CompletionOptions &p) { // NOLINT(misc-header-implementation)
  if (j.contains("triggerCharacters") && j.at("triggerCharacters").is_array())
    p.triggerCharacters = j.at("triggerCharacters").get<std::vector<std::string>>();
  else
    p.triggerCharacters = std::nullopt;
  if (j.contains("allCommitCharacters") && j.at("allCommitCharacters").is_array())
    p.allCommitCharacters = j.at("allCommitCharacters").get<std::vector<std::string>>();
  else
    p.allCommitCharacters = std::nullopt;
  if (j.contains("resolveProvider") && j.at("resolveProvider").is_boolean())
    p.resolveProvider = j.at("resolveProvider").get<bool>();
  else
    p.resolveProvider = std::nullopt;
}

/**
 * @brief 签名帮助选项。
 */
struct SignatureHelpOptions {
  std::optional<std::vector<std::string>> triggerCharacters; // 触发签名帮助的字符
  std::optional<std::vector<std::string>>
      retriggerCharacters; // 重新触发签名帮助的字符 (当签名帮助已激活时)
};

// 为 SignatureHelpOptions 提供 ADL 序列化函数 (实现补充)
inline void to_json(json &j, const SignatureHelpOptions &p) { // NOLINT(misc-header-implementation)
  j = json::object();
  if (p.triggerCharacters.has_value())
    j["triggerCharacters"] = p.triggerCharacters.value();
  if (p.retriggerCharacters.has_value())
    j["retriggerCharacters"] = p.retriggerCharacters.value();
}

inline void from_json(const json &j,
                      SignatureHelpOptions &p) { // NOLINT(misc-header-implementation)
  if (j.contains("triggerCharacters") && j.at("triggerCharacters").is_array())
    p.triggerCharacters = j.at("triggerCharacters").get<std::vector<std::string>>();
  else
    p.triggerCharacters = std::nullopt;
  if (j.contains("retriggerCharacters") && j.at("retriggerCharacters").is_array())
    p.retriggerCharacters = j.at("retriggerCharacters").get<std::vector<std::string>>();
  else
    p.retriggerCharacters = std::nullopt;
}

/**
 * @brief 服务器对外声明的能力 (扩展版)。
 * 注意：对于复杂能力（如 CodeActionOptions, RenameOptions 等），此结构体仅定义了
 * std::optional<bool> 或 std::optional<json>。 完整的实现需要定义相应的 Options
 * 结构体并在此处使用。
 */
struct ServerCapabilities {
  // 文档同步能力: 可以是简单的 Kind，也可以是详细的 Options 对象
  std::optional<std::variant<TextDocumentSyncKind, TextDocumentSyncOptions>> textDocumentSync;

  std::optional<bool> hoverProvider = false;           // 悬停提示提供者 (bool 或 HoverOptions)
  std::optional<CompletionOptions> completionProvider; // 代码补全提供者选项
  std::optional<SignatureHelpOptions> signatureHelpProvider; // 签名帮助提供者选项
  std::optional<bool> definitionProvider = true; // 跳转到定义提供者 (bool 或 DefinitionOptions)
  std::optional<bool> typeDefinitionProvider =
      false; // 跳转到类型定义提供者 (bool 或 TypeDefinitionOptions)
  std::optional<bool> implementationProvider =
      false; // 跳转到实现提供者 (bool 或 ImplementationOptions)
  std::optional<bool> referencesProvider = false; // 查找引用提供者 (bool 或 ReferenceOptions)
  std::optional<bool> documentHighlightProvider =
      false; // 文档高亮提供者 (bool 或 DocumentHighlightOptions)
  std::optional<bool> documentSymbolProvider =
      false; // 文档符号提供者 (bool 或 DocumentSymbolOptions)
  std::optional<bool> workspaceSymbolProvider =
      false; // 工作区符号提供者 (bool 或 WorkspaceSymbolOptions)
  std::optional<bool> codeActionProvider = false; // 代码动作提供者 (bool 或 CodeActionOptions)
  std::optional<json> codeLensProvider;     // 代码镜头提供者 (CodeLensOptions) - 使用 json 简化
  std::optional<json> documentLinkProvider; // 文档链接提供者 (DocumentLinkOptions) - 使用 json 简化
  std::optional<json> colorProvider; // 颜色提供者 (bool 或 DocumentColorOptions) - 使用 json 简化
  std::optional<bool> documentFormattingProvider =
      false; // 文档格式化提供者 (bool 或 DocumentFormattingOptions)
  std::optional<bool> documentRangeFormattingProvider =
      false; // 范围格式化提供者 (bool 或 DocumentRangeFormattingOptions)
  std::optional<json>
      documentOnTypeFormattingProvider; // 输入时格式化提供者 (DocumentOnTypeFormattingOptions) -
                                        // 使用 json 简化
  std::optional<bool> renameProvider = false; // 重命名提供者 (bool 或 RenameOptions)
  std::optional<json>
      foldingRangeProvider; // 代码折叠提供者 (bool 或 FoldingRangeOptions) - 使用 json 简化
  std::optional<json>
      selectionRangeProvider; // 选择范围提供者 (bool 或 SelectionRangeOptions) - 使用 json 简化
  std::optional<json>
      semanticTokensProvider; // 语义令牌提供者 (SemanticTokensOptions | ...) - 使用 json 简化
  std::optional<json> linkedEditingRangeProvider; // 链接编辑范围提供者 (bool or
                                                  // LinkedEditingRangeOptions) - 使用 json 简化
  // ... 其他能力 ...
  std::optional<json> workspace;    // 工作区相关能力 (例如 workspaceFolders 支持)
  std::optional<json> experimental; // 实验性能力
};

// 为 ServerCapabilities 提供显式 ADL 转换函数 (实现补充)
inline void to_json(json &j, const ServerCapabilities &p) { // NOLINT(misc-header-implementation)
  j = json::object();                                       // 初始化为空对象

  if (p.textDocumentSync.has_value()) {
    std::visit([&j](auto &&arg) { j["textDocumentSync"] = arg; }, p.textDocumentSync.value());
  }
  if (p.hoverProvider.has_value() && p.hoverProvider.value())
    j["hoverProvider"] = true; // 简化：只在 true 时发送
  if (p.completionProvider.has_value())
    j["completionProvider"] = p.completionProvider.value();
  if (p.signatureHelpProvider.has_value())
    j["signatureHelpProvider"] = p.signatureHelpProvider.value();
  if (p.definitionProvider.has_value() && p.definitionProvider.value())
    j["definitionProvider"] = true;
  if (p.typeDefinitionProvider.has_value() && p.typeDefinitionProvider.value())
    j["typeDefinitionProvider"] = true;
  if (p.implementationProvider.has_value() && p.implementationProvider.value())
    j["implementationProvider"] = true;
  if (p.referencesProvider.has_value() && p.referencesProvider.value())
    j["referencesProvider"] = true;
  if (p.documentHighlightProvider.has_value() && p.documentHighlightProvider.value())
    j["documentHighlightProvider"] = true;
  if (p.documentSymbolProvider.has_value() && p.documentSymbolProvider.value())
    j["documentSymbolProvider"] = true;
  if (p.workspaceSymbolProvider.has_value() && p.workspaceSymbolProvider.value())
    j["workspaceSymbolProvider"] = true;
  if (p.codeActionProvider.has_value() && p.codeActionProvider.value())
    j["codeActionProvider"] = true; // 简化：实际应为 CodeActionOptions
  if (p.codeLensProvider.has_value())
    j["codeLensProvider"] = p.codeLensProvider.value(); // 假设是 CodeLensOptions
  if (p.documentLinkProvider.has_value())
    j["documentLinkProvider"] = p.documentLinkProvider.value(); // 假设是 DocumentLinkOptions
  if (p.colorProvider.has_value())
    j["colorProvider"] = p.colorProvider.value(); // 假设是 DocumentColorOptions or bool
  if (p.documentFormattingProvider.has_value() && p.documentFormattingProvider.value())
    j["documentFormattingProvider"] = true;
  if (p.documentRangeFormattingProvider.has_value() && p.documentRangeFormattingProvider.value())
    j["documentRangeFormattingProvider"] = true;
  if (p.documentOnTypeFormattingProvider.has_value())
    j["documentOnTypeFormattingProvider"] =
        p.documentOnTypeFormattingProvider.value(); // 假设是 DocumentOnTypeFormattingOptions
  if (p.renameProvider.has_value() && p.renameProvider.value())
    j["renameProvider"] = true; // 简化：实际应为 RenameOptions
  if (p.foldingRangeProvider.has_value())
    j["foldingRangeProvider"] = p.foldingRangeProvider.value(); // 假设是 FoldingRangeOptions
  if (p.selectionRangeProvider.has_value())
    j["selectionRangeProvider"] = p.selectionRangeProvider.value(); // 假设是 SelectionRangeOptions
  if (p.semanticTokensProvider.has_value())
    j["semanticTokensProvider"] = p.semanticTokensProvider.value(); // 假设是 SemanticTokensOptions
  if (p.linkedEditingRangeProvider.has_value())
    j["linkedEditingRangeProvider"] =
        p.linkedEditingRangeProvider.value(); // 假设是 LinkedEditingRangeOptions
  if (p.workspace.has_value())
    j["workspace"] = p.workspace.value();
  if (p.experimental.has_value())
    j["experimental"] = p.experimental.value();
}

// 注意：服务器通常不需要反序列化 ServerCapabilities，此 from_json 主要用于测试或特殊场景
inline void from_json(const json &j, ServerCapabilities &p) { // NOLINT(misc-header-implementation)
  if (j.contains("textDocumentSync")) {
    const auto &sync = j.at("textDocumentSync");
    if (sync.is_number()) {
      p.textDocumentSync = sync.get<TextDocumentSyncKind>();
    } else if (sync.is_object()) {
      p.textDocumentSync = sync.get<TextDocumentSyncOptions>();
    } else {
      p.textDocumentSync = std::nullopt;
    }
  } else {
    p.textDocumentSync = std::nullopt;
  }

  if (j.contains("hoverProvider") && j.at("hoverProvider").is_boolean())
    p.hoverProvider = j.at("hoverProvider").get<bool>();
  else
    p.hoverProvider = std::nullopt; // 简化
  if (j.contains("completionProvider"))
    p.completionProvider = j.at("completionProvider").get<CompletionOptions>();
  else
    p.completionProvider = std::nullopt;
  if (j.contains("signatureHelpProvider"))
    p.signatureHelpProvider = j.at("signatureHelpProvider").get<SignatureHelpOptions>();
  else
    p.signatureHelpProvider = std::nullopt;
  if (j.contains("definitionProvider") && j.at("definitionProvider").is_boolean())
    p.definitionProvider = j.at("definitionProvider").get<bool>();
  else
    p.definitionProvider = std::nullopt; // 简化
  if (j.contains("typeDefinitionProvider") && j.at("typeDefinitionProvider").is_boolean())
    p.typeDefinitionProvider = j.at("typeDefinitionProvider").get<bool>();
  else
    p.typeDefinitionProvider = std::nullopt; // 简化
  if (j.contains("implementationProvider") && j.at("implementationProvider").is_boolean())
    p.implementationProvider = j.at("implementationProvider").get<bool>();
  else
    p.implementationProvider = std::nullopt; // 简化
  if (j.contains("referencesProvider") && j.at("referencesProvider").is_boolean())
    p.referencesProvider = j.at("referencesProvider").get<bool>();
  else
    p.referencesProvider = std::nullopt; // 简化
  if (j.contains("documentHighlightProvider") && j.at("documentHighlightProvider").is_boolean())
    p.documentHighlightProvider = j.at("documentHighlightProvider").get<bool>();
  else
    p.documentHighlightProvider = std::nullopt; // 简化
  if (j.contains("documentSymbolProvider") && j.at("documentSymbolProvider").is_boolean())
    p.documentSymbolProvider = j.at("documentSymbolProvider").get<bool>();
  else
    p.documentSymbolProvider = std::nullopt; // 简化
  if (j.contains("workspaceSymbolProvider") && j.at("workspaceSymbolProvider").is_boolean())
    p.workspaceSymbolProvider = j.at("workspaceSymbolProvider").get<bool>();
  else
    p.workspaceSymbolProvider = std::nullopt; // 简化
  if (j.contains("codeActionProvider") && j.at("codeActionProvider").is_boolean())
    p.codeActionProvider = j.at("codeActionProvider").get<bool>();
  else
    p.codeActionProvider = std::nullopt; // 简化
  if (j.contains("codeLensProvider") && !j.at("codeLensProvider").is_null())
    p.codeLensProvider = j.at("codeLensProvider");
  else
    p.codeLensProvider = std::nullopt;
  if (j.contains("documentLinkProvider") && !j.at("documentLinkProvider").is_null())
    p.documentLinkProvider = j.at("documentLinkProvider");
  else
    p.documentLinkProvider = std::nullopt;
  if (j.contains("colorProvider") && !j.at("colorProvider").is_null())
    p.colorProvider = j.at("colorProvider");
  else
    p.colorProvider = std::nullopt;
  if (j.contains("documentFormattingProvider") && j.at("documentFormattingProvider").is_boolean())
    p.documentFormattingProvider = j.at("documentFormattingProvider").get<bool>();
  else
    p.documentFormattingProvider = std::nullopt;
  if (j.contains("documentRangeFormattingProvider") &&
      j.at("documentRangeFormattingProvider").is_boolean())
    p.documentRangeFormattingProvider = j.at("documentRangeFormattingProvider").get<bool>();
  else
    p.documentRangeFormattingProvider = std::nullopt;
  if (j.contains("documentOnTypeFormattingProvider") &&
      !j.at("documentOnTypeFormattingProvider").is_null())
    p.documentOnTypeFormattingProvider = j.at("documentOnTypeFormattingProvider");
  else
    p.documentOnTypeFormattingProvider = std::nullopt;
  if (j.contains("renameProvider") && j.at("renameProvider").is_boolean())
    p.renameProvider = j.at("renameProvider").get<bool>();
  else
    p.renameProvider = std::nullopt; // 简化
  if (j.contains("foldingRangeProvider") && !j.at("foldingRangeProvider").is_null())
    p.foldingRangeProvider = j.at("foldingRangeProvider");
  else
    p.foldingRangeProvider = std::nullopt;
  if (j.contains("selectionRangeProvider") && !j.at("selectionRangeProvider").is_null())
    p.selectionRangeProvider = j.at("selectionRangeProvider");
  else
    p.selectionRangeProvider = std::nullopt;
  if (j.contains("semanticTokensProvider") && !j.at("semanticTokensProvider").is_null())
    p.semanticTokensProvider = j.at("semanticTokensProvider");
  else
    p.semanticTokensProvider = std::nullopt;
  if (j.contains("linkedEditingRangeProvider") && !j.at("linkedEditingRangeProvider").is_null())
    p.linkedEditingRangeProvider = j.at("linkedEditingRangeProvider");
  else
    p.linkedEditingRangeProvider = std::nullopt;
  if (j.contains("workspace") && !j.at("workspace").is_null())
    p.workspace = j.at("workspace");
  else
    p.workspace = std::nullopt;
  if (j.contains("experimental") && !j.at("experimental").is_null())
    p.experimental = j.at("experimental");
  else
    p.experimental = std::nullopt;
}

/**
 * @brief initialize 请求的成功响应结果。
 */
struct InitializeResult {
  ServerCapabilities capabilities; // 服务器提供的能力 (必需)
  std::optional<json> serverInfo;  // 服务器信息 {name: string, version?: string} (可选)
};

// 为 InitializeResult 提供显式 ADL 转换函数
inline void to_json(json &j, const InitializeResult &p) { // NOLINT(misc-header-implementation)
  j = json{{"capabilities", p.capabilities}};
  if (p.serverInfo.has_value())
    j["serverInfo"] = p.serverInfo.value();
}

inline void from_json(const json &j, InitializeResult &p) { // NOLINT(misc-header-implementation)
  j.at("capabilities").get_to(p.capabilities);
  if (j.contains("serverInfo") && !j.at("serverInfo").is_null())
    p.serverInfo = j.at("serverInfo");
  else
    p.serverInfo = std::nullopt;
}

// ==========================================================================
// 语言特性相关参数和结果 (补充)
// ==========================================================================

/**
 * @brief 包含文档和位置信息的通用参数结构，用于多种请求。
 */
struct TextDocumentPositionParams {
  TextDocumentIdentifier textDocument; // 目标文本文档 (必需)
  Position position;                   // 文档内的位置 (必需)
};

// 为 TextDocumentPositionParams 提供显式 ADL 转换函数
inline void to_json(json &j, const TextDocumentPositionParams &p) {
  j = json{{"textDocument", p.textDocument}, {"position", p.position}};
} // NOLINT(misc-header-implementation)

inline void from_json(const json &j, TextDocumentPositionParams &p) {
  j.at("textDocument").get_to(p.textDocument);
  j.at("position").get_to(p.position);
} // NOLINT(misc-header-implementation)

// --- 代码补全 ---

/**
 * @brief 触发代码补全的原因。
 */
enum class CompletionTriggerKind {
  Invoked = 1,                        // 用户手动调用 (例如 Ctrl+Space)
  TriggerCharacter = 2,               // 输入了触发字符 (例如 '.')
  TriggerForIncompleteCompletions = 3 // 重新触发以获取之前不完整的列表
};

// 为 CompletionTriggerKind 提供 ADL 转换函数 (实现补充)
inline void to_json(json &j, const CompletionTriggerKind &e) {
  j = static_cast<std::underlying_type_t<CompletionTriggerKind>>(e);
} // NOLINT(misc-header-implementation)

inline void from_json(const json &j,
                      CompletionTriggerKind &e) { // NOLINT(misc-header-implementation)
  if (!j.is_number_integer()) {
    throw std::runtime_error("CompletionTriggerKind must be an integer");
  }
  auto value = j.get<std::underlying_type_t<CompletionTriggerKind>>();
  switch (value) {
  case 1:
    e = CompletionTriggerKind::Invoked;
    break;
  case 2:
    e = CompletionTriggerKind::TriggerCharacter;
    break;
  case 3:
    e = CompletionTriggerKind::TriggerForIncompleteCompletions;
    break;
  default:
    throw std::runtime_error("Invalid value for CompletionTriggerKind");
  }
}

/**
 * @brief 代码补全请求的上下文信息。
 */
struct CompletionContext {
  CompletionTriggerKind triggerKind; // 触发类型 (必需)
  std::optional<std::string>
      triggerCharacter; // 如果是 TriggerCharacter 触发，则为触发的字符 (可选)
};

// 为 CompletionContext 提供 ADL 序列化函数 (实现补充)
inline void to_json(json &j, const CompletionContext &p) { // NOLINT(misc-header-implementation)
  j = json{{"triggerKind", p.triggerKind}};
  if (p.triggerCharacter.has_value()) {
    j["triggerCharacter"] = p.triggerCharacter.value();
  }
}

inline void from_json(const json &j, CompletionContext &p) { // NOLINT(misc-header-implementation)
  j.at("triggerKind").get_to(p.triggerKind);
  if (j.contains("triggerCharacter") && j.at("triggerCharacter").is_string()) {
    p.triggerCharacter = j.at("triggerCharacter").get<std::string>();
  } else {
    p.triggerCharacter = std::nullopt;
  }
}

/**
 * @brief textDocument/completion 请求的参数。
 */
struct CompletionParams : TextDocumentPositionParams {
  std::optional<CompletionContext> context; // 补全上下文 (可选)
};

// 为 CompletionParams 提供 ADL 序列化函数 (实现补充)
inline void to_json(json &j, const CompletionParams &p) { // NOLINT(misc-header-implementation)
  // 先序列化基类
  to_json(j, static_cast<const TextDocumentPositionParams &>(p));
  // 再添加子类字段
  if (p.context.has_value())
    j["context"] = p.context.value();
}

inline void from_json(const json &j, CompletionParams &p) { // NOLINT(misc-header-implementation)
  // 先反序列化基类
  from_json(j, static_cast<TextDocumentPositionParams &>(p));
  // 再解析子类字段
  if (j.contains("context"))
    p.context = j.at("context").get<CompletionContext>();
  else
    p.context = std::nullopt;
}

/**
 * @brief 代码补全项的种类 (Kind)。
 * 数值与 LSP 规范一致。
 */
enum class CompletionItemKind {
  Text = 1,
  Method = 2,
  Function = 3,
  Constructor = 4,
  Field = 5,
  Variable = 6,
  Class = 7,
  Interface = 8,
  Module = 9,
  Property = 10,
  Unit = 11,
  Value = 12,
  Enum = 13,
  Keyword = 14,
  Snippet = 15,
  Color = 16,
  File = 17,
  Reference = 18,
  Folder = 19,
  EnumMember = 20,
  Constant = 21,
  Struct = 22,
  Event = 23,
  Operator = 24,
  TypeParameter = 25
};

// 为 CompletionItemKind 提供显式 ADL 转换函数
inline void to_json(json &j, const CompletionItemKind &e) {
  j = static_cast<std::underlying_type_t<CompletionItemKind>>(e);
} // NOLINT(misc-header-implementation)

inline void from_json(const json &j, CompletionItemKind &e) { // NOLINT(misc-header-implementation)
  if (!j.is_number_integer()) {
    throw std::runtime_error("CompletionItemKind must be an integer");
  }
  auto value = j.get<std::underlying_type_t<CompletionItemKind>>();
  // 简单转换，严格来说应检查范围
  if (value >= static_cast<std::underlying_type_t<CompletionItemKind>>(CompletionItemKind::Text) &&
      value <= static_cast<std::underlying_type_t<CompletionItemKind>>(
                   CompletionItemKind::TypeParameter)) {
    e = static_cast<CompletionItemKind>(value);
  } else {
    // 对于未知或无效的值，可以抛出异常或赋一个默认值
    // throw std::runtime_error("Invalid value for CompletionItemKind");
    e = CompletionItemKind::Text; // 示例：赋默认值
  }
}

// --- 悬停提示 ---

/**
 * @brief 标记内容，用于 Hover 等，支持 Markdown 或纯文本。
 */
struct MarkupContent {
  std::string kind = "markdown"; // 内容类型："markdown" 或 "plaintext" (必需)
  std::string value;             // 内容文本 (必需)
};

// 为 MarkupContent 提供显式 ADL 转换函数
inline void to_json(json &j, const MarkupContent &p) {
  j = json{{"kind", p.kind}, {"value", p.value}};
} // NOLINT(misc-header-implementation)

inline void from_json(const json &j, MarkupContent &p) {
  j.at("kind").get_to(p.kind);
  j.at("value").get_to(p.value);
} // NOLINT(misc-header-implementation)

/**
 * @brief 悬停信息结果。
 */
struct Hover {
  // 内容可以是单个 MarkupContent，也可以是 MarkedString[] 或 MarkupContent[] (需要更复杂的处理)
  // 这里简化为单个 MarkupContent
  MarkupContent contents;     // 显示的内容 (必需)
  std::optional<Range> range; // 信息对应的代码范围 (可选)
};

// 为 Hover 提供显式 ADL 转换函数
inline void to_json(json &j, const Hover &p) { // NOLINT(misc-header-implementation)
  j = json{{"contents", p.contents}};
  if (p.range.has_value()) {
    j["range"] = p.range.value();
  }
}

inline void from_json(const json &j, Hover &p) { // NOLINT(misc-header-implementation)
  // 注意: 实际 LSP 中 contents 可以是多种类型，这里简化为只处理 MarkupContent
  if (j.contains("contents")) {
    // 尝试解析为 MarkupContent，如果失败则根据需要处理或抛出
    try {
      j.at("contents").get_to(p.contents);
    } catch (const json::exception &e) {
      // 可能需要处理 MarkedString 或数组等情况
      // 为了简单起见，这里可以记录错误或将其视为空内容
      p.contents = {"plaintext", ""}; // 示例：设置为空白内容
      // 或者抛出: throw std::runtime_error("Failed to parse Hover contents as MarkupContent");
    }
  } else {
    // contents 是必需的，但为了健壮性，可以设置默认值
    p.contents = {"plaintext", ""};
  }

  if (j.contains("range") && !j.at("range").is_null()) {
    p.range = j.at("range").get<Range>();
  } else {
    p.range = std::nullopt;
  }
}

/**
 * @brief 定义补全项插入文本的格式。
 */
enum class InsertTextFormat {
  PlainText = 1, // 插入的文本是纯文本
  Snippet = 2    // 插入的文本是 LSP 代码片段格式
};

// 为 InsertTextFormat 提供 ADL 转换函数
inline void to_json(json &j, const InsertTextFormat &e) { // NOLINT(misc-header-implementation)
  j = static_cast<std::underlying_type_t<InsertTextFormat>>(e);
}

inline void from_json(const json &j, InsertTextFormat &e) { // NOLINT(misc-header-implementation)
  if (!j.is_number_integer()) {
    throw std::runtime_error("InsertTextFormat must be an integer");
  }
  auto value = j.get<std::underlying_type_t<InsertTextFormat>>();
  switch (value) {
  case 1:
    e = InsertTextFormat::PlainText;
    break;
  case 2:
    e = InsertTextFormat::Snippet;
    break;
  default:
    throw std::runtime_error("Invalid value for InsertTextFormat");
  }
}

/**
 * @brief 单个代码补全项 (简化版)。
 * 真实的 CompletionItem 非常复杂，包含编辑、过滤、排序等信息。
 * 此结构体定义省略了许多可选字段。
 */
struct CompletionItem {
  std::string label;                              // 显示的文本 (必需)
  std::optional<CompletionItemKind> kind;         // 类型图标 (可选)
  std::optional<std::vector<DiagnosticTag>> tags; // 标签 (例如 Deprecated)
  std::optional<std::string> detail;              // 额外细节, 如类型签名 (可选)
  std::optional<std::variant<std::string, MarkupContent>>
      documentation;                                // 文档字符串或标记内容 (可选)
  std::optional<bool> deprecated;                   // 是否已废弃 (建议使用 tags)
  std::optional<bool> preselect;                    // 是否默认选中 (可选)
  std::optional<std::string> sortText;              // 用于排序的文本 (可选)
  std::optional<std::string> filterText;            // 用于过滤的文本 (可选)
  std::optional<std::string> insertText;            // 插入的文本 (可选, 默认是 label)
  std::optional<InsertTextFormat> insertTextFormat; // 定义插入文本的格式
  // 更多字段如 textEdit, additionalTextEdits, commitCharacters, command, data 在此省略
};

// 为 CompletionItem 提供显式 ADL 转换函数 (简化版)
inline void to_json(json &j, const CompletionItem &p) { // NOLINT(misc-header-implementation)
  j = json{{"label", p.label}};
  if (p.kind.has_value())
    j["kind"] = p.kind.value();
  if (p.tags.has_value() && !p.tags.value().empty())
    j["tags"] = p.tags.value();
  if (p.detail.has_value())
    j["detail"] = p.detail.value();
  if (p.documentation.has_value()) {
    std::visit([&j](auto &&arg) { j["documentation"] = arg; }, p.documentation.value());
  }
  if (p.deprecated.has_value())
    j["deprecated"] = p.deprecated.value();
  if (p.preselect.has_value())
    j["preselect"] = p.preselect.value();
  if (p.sortText.has_value())
    j["sortText"] = p.sortText.value();
  if (p.filterText.has_value())
    j["filterText"] = p.filterText.value();
  if (p.insertText.has_value())
    j["insertText"] = p.insertText.value();
  if (p.insertTextFormat.has_value())
    j["insertTextFormat"] = p.insertTextFormat.value();
  // ... 序列化其他字段 ...
}

inline void from_json(const json &j, CompletionItem &p) { // NOLINT(misc-header-implementation)
  j.at("label").get_to(p.label);
  if (j.contains("kind") && !j.at("kind").is_null())
    p.kind = j.at("kind").get<CompletionItemKind>();
  else
    p.kind = std::nullopt;
  if (j.contains("tags") && j.at("tags").is_array())
    p.tags = j.at("tags").get<std::vector<DiagnosticTag>>();
  else
    p.tags = std::nullopt;
  if (j.contains("detail") && j.at("detail").is_string())
    p.detail = j.at("detail").get<std::string>();
  else
    p.detail = std::nullopt;
  if (j.contains("documentation") && !j.at("documentation").is_null()) {
    const auto &doc_json = j.at("documentation");
    if (doc_json.is_string()) {
      p.documentation = doc_json.get<std::string>();
    } else if (doc_json.is_object()) {
      p.documentation = doc_json.get<MarkupContent>();
    } else {
      p.documentation = std::nullopt;
    }
  } else {
    p.documentation = std::nullopt;
  }
  if (j.contains("deprecated") && j.at("deprecated").is_boolean())
    p.deprecated = j.at("deprecated").get<bool>();
  else
    p.deprecated = std::nullopt;
  if (j.contains("preselect") && j.at("preselect").is_boolean())
    p.preselect = j.at("preselect").get<bool>();
  else
    p.preselect = std::nullopt;
  if (j.contains("sortText") && j.at("sortText").is_string())
    p.sortText = j.at("sortText").get<std::string>();
  else
    p.sortText = std::nullopt;
  if (j.contains("filterText") && j.at("filterText").is_string())
    p.filterText = j.at("filterText").get<std::string>();
  else
    p.filterText = std::nullopt;
  if (j.contains("insertText") && j.at("insertText").is_string())
    p.insertText = j.at("insertText").get<std::string>();
  else
    p.insertText = std::nullopt;
  if (j.contains("insertTextFormat") && !j.at("insertTextFormat").is_null())
    p.insertTextFormat = j.at("insertTextFormat").get<InsertTextFormat>();
  else
    p.insertTextFormat = std::nullopt;
  // ... 反序列化其他字段 ...
}

/**
 * @brief 代码补全列表。可以是 CompletionItem 数组或此结构。
 */
struct CompletionList {
  bool isIncomplete = false;         // 如果列表不完整 (例如结果太多)，设为 true
  std::vector<CompletionItem> items; // 补全项列表 (必需)
};

// 为 CompletionList 提供显式 ADL 转换函数
inline void to_json(json &j, const CompletionList &p) {
  j = json{{"isIncomplete", p.isIncomplete}, {"items", p.items}};
} // NOLINT(misc-header-implementation)

inline void from_json(const json &j, CompletionList &p) {
  j.at("isIncomplete").get_to(p.isIncomplete);
  j.at("items").get_to(p.items);
} // NOLINT(misc-header-implementation)

// 注意: completion 请求的响应可以是 CompletionItem[] 或 CompletionList 或 null。

// ==========================================================================
// JSON-RPC 基础结构
// ==========================================================================

/**
 * @brief JSON-RPC 请求/通知 ID 类型 (string | integer | null)。
 * 使用 std::variant 来表示多种可能性。null 主要用于响应解析请求失败的情况。
 */
using JsonRpcId = std::variant<std::string, int, std::nullptr_t>;

/**
 * @brief JSON-RPC 基础消息，包含版本号。
 */
struct JsonRpcMessage {
  std::string jsonrpc = "2.0"; // JSON-RPC 版本号，固定为 "2.0"
};

/**
 * @brief JSON-RPC 请求消息。必须包含 id。
 */
struct JsonRpcRequest : JsonRpcMessage {
  JsonRpcId id;               // 请求 ID (必需, string 或 int)
  std::string method;         // 请求的方法名 (必需)
  std::optional<json> params; // 请求参数 (可选, 如果没有参数，则省略或为 null)
};

// 为 JsonRpcRequest 提供显式 ADL 转换函数
inline void to_json(json &j, const JsonRpcRequest &r) { // NOLINT(misc-header-implementation)
  j = json{{"jsonrpc", r.jsonrpc}, {"method", r.method}};
  // 访问并序列化 variant 类型的 id
  std::visit(
      [&j](auto &&arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (!std::is_same_v<T, std::nullptr_t>) { // 请求 ID 不应为 null
          j["id"] = arg;
        } else {
          // 规范要求请求 ID 不为 null，严格模式下应抛出错误
          throw std::runtime_error("Request ID cannot be null when serializing.");
        }
      },
      r.id);
  if (r.params.has_value()) {
    j["params"] = r.params.value();
  }
  // 如果 params 是 nullopt，则 JSON 中省略 params 字段
}

inline void from_json(const json &j, JsonRpcRequest &r) { // NOLINT(misc-header-implementation)
  j.at("jsonrpc").get_to(r.jsonrpc);
  j.at("method").get_to(r.method);
  // 解析 id (必需)
  if (j.contains("id")) {
    const auto &id_json = j.at("id");
    if (id_json.is_string()) {
      r.id = id_json.get<std::string>();
    } else if (id_json.is_number_integer()) {
      r.id = id_json.get<int>();
    } else {
      throw std::runtime_error("Request 'id' must be a string or an integer.");
    }
  } else {
    throw std::runtime_error("Request message must have an 'id' field.");
  }
  // 解析可选的 params
  if (j.contains("params") && !j.at("params").is_null()) {
    r.params = j.at("params");
  } else {
    r.params = std::nullopt;
  }
}

/**
 * @brief JSON-RPC 错误对象。
 */
struct JsonRpcError {
  int code;                 // 错误代码 (必需)
  std::string message;      // 错误消息 (必需)
  std::optional<json> data; // 附加数据 (可选)
};

// 为 JsonRpcError 提供显式 ADL 转换函数
inline void to_json(json &j, const JsonRpcError &e) { // NOLINT(misc-header-implementation)
  j = json{{"code", e.code}, {"message", e.message}};
  if (e.data.has_value()) {
    j["data"] = e.data.value();
  }
}

inline void from_json(const json &j, JsonRpcError &e) { // NOLINT(misc-header-implementation)
  j.at("code").get_to(e.code);
  j.at("message").get_to(e.message);
  if (j.contains("data") && !j.at("data").is_null()) {
    e.data = j.at("data");
  } else {
    e.data = std::nullopt;
  }
}

/**
 * @brief JSON-RPC 响应消息。必须包含 id。
 * result 和 error 字段必须存在一个且仅存在一个。
 */
struct JsonRpcResponse : JsonRpcMessage {
  JsonRpcId id;                      // 对应的请求 ID (必需, 可以是 string, int 或 null)
  std::optional<json> result;        // 成功时的结果 (可选)
  std::optional<JsonRpcError> error; // 失败时的错误对象 (可选)
};

// 为 JsonRpcResponse 提供显式 ADL 转换函数
inline void to_json(json &j, const JsonRpcResponse &r) { // NOLINT(misc-header-implementation)
  j = json{{"jsonrpc", r.jsonrpc}};
  // 序列化 id (可以是 string, int 或 null)
  std::visit([&j](auto &&arg) { j["id"] = arg; }, r.id);

  if (r.result.has_value() && r.error.has_value()) {
    // 不符合规范，优先发送 error (或抛出异常)
    j["error"] = r.error.value();
    // throw std::logic_error("JsonRpcResponse cannot have both result and error.");
  } else if (r.result.has_value()) {
    j["result"] = r.result.value();
  } else if (r.error.has_value()) {
    j["error"] = r.error.value();
  } else {
    // 规范要求必须有 result 或 error，对于空结果，发送 result: null
    j["result"] = nullptr;
  }
}

inline void from_json(const json &j, JsonRpcResponse &r) { // NOLINT(misc-header-implementation)
  j.at("jsonrpc").get_to(r.jsonrpc);
  // 解析 id (可以是 string, int 或 null)
  if (j.contains("id")) {
    const auto &id_json = j.at("id");
    if (id_json.is_string()) {
      r.id = id_json.get<std::string>();
    } else if (id_json.is_number_integer()) {
      r.id = id_json.get<int>();
    } else if (id_json.is_null()) {
      r.id = nullptr;
    } else {
      throw std::runtime_error("Response 'id' must be a string, an integer, or null.");
    }
  } else {
    // 对于某些错误响应（如 ParseError），可能没有 id，但这不符合严格的 JSON-RPC 2.0 响应规范
    // 这里选择严格模式，要求必须有 id
    throw std::runtime_error("Response message must have an 'id' field.");
  }
  // 安全地获取可选字段 result 和 error
  bool has_result = j.contains("result");
  bool has_error = j.contains("error");

  if (has_result && has_error) {
    // 不符合规范，优先取 error
    r.error = j.at("error").get<JsonRpcError>();
    r.result = std::nullopt;
    // throw std::runtime_error("Response cannot have both 'result' and 'error' fields.");
  } else if (has_result) {
    r.result = j.at("result"); // result 可以是任何 JSON 值，包括 null
    r.error = std::nullopt;
  } else if (has_error && !j.at("error").is_null()) {
    r.error = j.at("error").get<JsonRpcError>();
    r.result = std::nullopt;
  } else {
    // 既没有 result 也没有非 null 的 error，不符合规范
    throw std::runtime_error("Response must have either a 'result' or a non-null 'error' field.");
    // 或者，为了健壮性，都设为 nullopt
    // r.result = std::nullopt;
    // r.error = std::nullopt;
  }
}

/**
 * @brief JSON-RPC 通知消息。没有 id。
 */
struct JsonRpcNotification : JsonRpcMessage {
  std::string method;         // 通知的方法名 (必需)
  std::optional<json> params; // 通知参数 (可选)
};

// 为 JsonRpcNotification 提供显式 ADL 转换函数
inline void to_json(json &j, const JsonRpcNotification &n) { // NOLINT(misc-header-implementation)
  j = json{{"jsonrpc", n.jsonrpc}, {"method", n.method}};
  if (n.params.has_value()) {
    j["params"] = n.params.value();
  }
}

inline void from_json(const json &j, JsonRpcNotification &n) { // NOLINT(misc-header-implementation)
  j.at("jsonrpc").get_to(n.jsonrpc);
  j.at("method").get_to(n.method);
  if (j.contains("params") && !j.at("params").is_null()) {
    n.params = j.at("params");
  } else {
    n.params = std::nullopt;
  }
}

// ==========================================================================
// 辅助函数模板声明
// ==========================================================================
/**
 * @brief 尝试将 JSON 值安全地解析为指定的 C++ 类型 T。
 * 在解析 LSP 消息参数时很有用。
 * @tparam T 目标 C++ 类型，必须有对应的 from_json 函数。
 * @param params_json 可选的 JSON 值 (通常是 params 字段)。
 * @return 如果 params_json 存在且不为 null 且解析成功，返回包含 T 实例的 std::optional；否则返回
 * std::nullopt。
 */
template <typename T> std::optional<T> tryParseParams(const std::optional<json> &params_json) {
  if (!params_json.has_value() || params_json.value().is_null()) {
    // 如果 params 不存在或是显式的 null，则认为无法解析为具体的 T 类型
    return std::nullopt;
  }
  try {
    // 尝试从存在的 json 值中解析 T
    return params_json.value().get<T>(); // 使用 get<T>() 会调用 T 的 from_json
  } catch (const json::exception & /*e*/) {
    // 可以在这里记录详细的解析错误 e.what()
    // std::cerr << "Failed to parse params as " << typeid(T).name() << ": " << e.what() <<
    // std::endl;
    return std::nullopt; // 解析失败返回空 optional
  }
}
