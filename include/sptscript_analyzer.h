#ifndef SPTSCRIPT_ANALYZER_H
#define SPTSCRIPT_ANALYZER_H

#include "document_manager.h"
#include "parser_engine.h"  // 引入 ParseResult 定义 (包含 Diagnostic, CST Root, Token Stream)
#include "protocol_types.h" // 引入 LSP 协议类型定义 (CompletionList, Hover, Location 等)
#include "semantic_info.h"  // 引入语义信息结构定义 (TypeInfo, SymbolInfo, Scope, SymbolTable)
#include "uri.h"            // 引入 Uri 类

#include <memory>        // 引入 C++ 标准内存管理库 (用于 shared_ptr, unique_ptr)
#include <mutex>         // 引入 C++ 标准互斥量库 (用于缓存线程安全)
#include <optional>      // 引入 C++ 标准可选类型
#include <set>           // 引入 C++ 标准集合容器 (用于跟踪分析中的文件)
#include <string>        // 引入 C++ 标准字符串库
#include <unordered_map> // 引入 C++ 标准无序映射容器 (用于缓存)
#include <utility>
#include <vector> // 引入 C++ 标准向量容器

// 前置声明 ANTLR 相关类
namespace antlr4::tree {
class ParseTree;
}
// 前置声明分析过程中可能使用的 Visitor 类
class SemanticAnalyzerVisitor; // 具体实现在 .cpp 中

/**
 * @brief 存储单个文档完整分析结果的结构体。
 * 包含语法和语义信息。
 */
/**
 * @brief 存储单个文档完整分析结果的结构体。
 * 包含语法和语义信息。
 */
struct AnalysisResult {
  Uri uri;
  long version = -1;
  //    std::vector<Diagnostic> syntaxDiagnostics;
  std::vector<Diagnostic> semanticDiagnostics;

  //    antlr4::tree::ParseTree* cstRoot = nullptr;
  std::shared_ptr<ParseResult> parseRes;

  // 语义分析结果
  std::unique_ptr<SymbolTable> symbolTable;
  bool semanticAnalysisDone = false;

  // --- 新增映射 ---
  /** @brief 将 CST 节点映射到其对应的已解析符号信息 */
  std::unordered_map<antlr4::tree::ParseTree *, SymbolInfoPtr> nodeSymbolMap;
  /** @brief 将源代码范围映射到包含该范围的最内层作用域 */
  std::map<Range, std::shared_ptr<Scope>> scopeRangeMap;

  // --- 结束新增 ---

  AnalysisResult(Uri u, long v, std::shared_ptr<ParseResult> parseRes)
      : uri(std::move(u)), version(v), parseRes(std::move(parseRes)) {
    symbolTable = std::make_unique<SymbolTable>();
    // ** 注意: 映射容器在这里默认构造即可 **
  }

  // 使 AnalysisResult 不可复制，但可移动
  AnalysisResult(const AnalysisResult &) = delete;
  AnalysisResult &operator=(const AnalysisResult &) = delete;
  AnalysisResult(AnalysisResult &&) = default;
  AnalysisResult &operator=(AnalysisResult &&) = default;
  // 默认析构函数会正确处理 unique_ptr 成员
  ~AnalysisResult() = default;
};

/**
 * @brief SptScript 语言分析服务类。
 * 负责解析、语义分析、缓存结果，并响应 LSP 功能请求。
 * 设计为线程安全，可供 LSP 服务器在不同请求处理中调用。
 */
class SptScriptAnalyzer {
private:
  // 使用共享指针管理缓存中的 AnalysisResult，允许多个请求共享同一份（不可变）分析结果
  std::unordered_map<Uri, std::shared_ptr<const AnalysisResult>> analysisCache;
  std::mutex cacheMtx; // 保护缓存访问的互斥锁

  /**
   * @brief 执行实际的解析和分析过程（私有辅助函数）。
   * 这是分析的核心，包含递归处理导入和循环检测的逻辑。
   * @param uri 要分析的文档 URI。
   * @param content 文档的完整内容。
   * @param version 文档的版本号。
   * @param visitedInCurrentStack 用于检测当前调用栈中循环导入的 URI 集合。
   * @param forceReanalysis 即使缓存有效也强制重新分析（例如，当依赖改变时）。
   * @return 指向分析结果的共享指针（const，表示结果不应被外部修改）。
   * 如果发生不可恢复错误（如文件无法读取）则返回 nullptr。
   */
  std::shared_ptr<const AnalysisResult> performAnalysis(const Uri &uri, const std::string &content,
                                                        long version,
                                                        std::set<Uri> &visitedInCurrentStack,
                                                        bool forceReanalysis = false);

  /**
   * @brief 实际执行语义分析的函数，通常涉及遍历 CST。
   * @param result 可变的 AnalysisResult 引用，用于填充语义信息。
   * @param visitedInCurrentStack 用于检测循环导入的集合。
   * @note 这个函数会创建并运行 SemanticAnalyzerVisitor。
   */
  void runSemanticPass(AnalysisResult &result, std::set<Uri> &visitedInCurrentStack);

  /**
   * @brief 从文件系统读取文件内容。
   * @param uri 目标文件的 URI。
   * @return 包含文件内容的 std::optional<std::string>，读取失败则返回 std::nullopt。
   */
  std::optional<std::string> readFileContent(const Uri &uri);

  TypeInfoPtr inferBaseExpressionTypeForMemberAccess( // 新增声明
      const std::shared_ptr<const AnalysisResult> &analysis, Position position,
      DocumentManager &docManager);

  /**
   * @brief 实现代码补全的核心逻辑。
   * @param analysis 指向常量分析结果的共享指针。
   * @param position 请求补全的位置。
   * @return 补全项列表。
   */
  std::vector<CompletionItem>
  calculateCompletions(const std::shared_ptr<const AnalysisResult> &analysis, Position position,
                       DocumentManager &docManager,
                       const std::optional<CompletionContext> &lspContext // <--- 新增参数
  );
  /**
   * @brief 实现悬停信息的核心逻辑。
   * @param analysis 指向常量分析结果的共享指针。
   * @param position 请求悬停信息的位置。
   * @return 可选的 Hover 信息。
   */
  std::optional<Hover> calculateHoverInfo(const std::shared_ptr<const AnalysisResult> &analysis,
                                          Position position);

  /**
   * @brief 实现查找定义的核心逻辑。
   * @param analysis 指向常量分析结果的共享指针。
   * @param position 请求查找定义的位置。
   * @return 可选的 Location 信息。
   */
  std::optional<DefinitionLink>
  findDefinitionLocation(const std::shared_ptr<const AnalysisResult> &analysis, Position position, DocumentManager &docManager);

  /**
   * @brief 辅助函数，安全地从缓存中获取分析结果。
   * @param uri 文档 URI。
   * @return 指向缓存结果的共享指针（const），如果不存在则为 nullptr。
   */
  std::shared_ptr<const AnalysisResult> getCachedAnalysis(const Uri &uri);

  // --- 新增私有辅助函数声明 ---
  /**
   * @brief 根据源代码位置查找最内层的作用域。
   * @param analysis 已完成的分析结果 (需要包含 scopeRangeMap)。
   * @param position 要查找的位置。
   * @return 指向包含该位置的最内层 Scope 的共享指针，若未找到则返回 nullptr。
   */
  std::shared_ptr<Scope> findScopeAtPosition(const AnalysisResult &analysis, Position position);

  /**
   * @brief 从给定的起始作用域开始，向上查找所有可见的符号。
   * @param startingScope 开始查找的作用域。
   * @param[out] results 用于存储找到的可见符号的向量 (会处理遮蔽)。
   */
  void findVisibleSymbols(std::shared_ptr<Scope> startingScope,
                          std::vector<SymbolInfoPtr> &results);
  // --- 结束新增 ---
public:
  // 默认构造函数
  SptScriptAnalyzer();

  // 禁止拷贝构造和拷贝赋值
  SptScriptAnalyzer(const SptScriptAnalyzer &) = delete;
  SptScriptAnalyzer &operator=(const SptScriptAnalyzer &) = delete;

  /**
   * @brief 根据 import 语句中的路径字符串和当前文件 URI 解析导入文件的绝对 URI。
   * @param currentUri 当前文件的 URI。
   * @param importString import 语句中的路径字符串 (例如 "./utils", "project/math")。
   * @return 解析成功则返回目标文件的 Uri，否则返回 std::nullopt。
   * @note 需要实现基于工作区、项目配置或相对路径的解析逻辑。
   */
  std::optional<Uri> resolveImportPath(const Uri &currentUri, const std::string &importString);

  /**
   * @brief 分析或重新分析指定的文档。
   * 这是 LSP 服务器在收到 didOpen 或 didChange 时调用的主要入口。
   * @param uri 文档的 URI。
   * @param content 文档的当前内容。
   * @param version 文档的当前版本号。
   * @return 指向（可能已更新的）缓存中分析结果的共享指针（const）。
   */
  std::shared_ptr<const AnalysisResult> analyzeDocument(const Uri &uri, const std::string &content,
                                                        long version);

  /**
   * @brief 获取指定文档的所有诊断信息（合并语法和语义）。
   * @param uri 文档的 URI。
   * @return 包含诊断信息的向量。如果文档未分析则为空。
   */
  std::vector<Diagnostic> getDiagnostics(const Uri &uri);

  /**
   * @brief 获取指定位置的代码补全建议。
   * @param uri 文档的 URI。
   * @param position 请求补全的位置。
   * @return 包含补全项的 CompletionList (可选)。
   */
  std::optional<CompletionList>
  getCompletions(const Uri &uri, Position position,
                 DocumentManager &docManager,                       // 已有
                 const std::optional<CompletionContext> &lspContext // <--- 新增参数
  );
  /**
   * @brief 获取指定位置的悬停信息。
   * @param uri 文档的 URI。
   * @param position 请求悬停信息的位置。
   * @return 包含悬停信息的 Hover 结构 (可选)。
   */
  std::optional<Hover> getHoverInfo(const Uri &uri, Position position);

  /**
   * @brief 查找指定位置符号的定义。
   * @param uri 文档的 URI。
   * @param position 请求跳转定义的位置。
   * @return 包含定义位置的 Location 结构 (可选)。
   */
  std::optional<DefinitionLink> findDefinition(const Uri &uri, Position position,
                                               DocumentManager &docManager);

  /**
   * @brief 当文档关闭时，从缓存中清除其分析结果，释放资源。
   * @param uri 要清除分析结果的文档 URI。
   */
  void clearAnalysis(const Uri &uri);

  std::shared_ptr<const AnalysisResult> analyzeDocument(const Uri &uri, const std::string &content,
                                                        long version,
                                                        std::set<Uri> &visitedInCurrentStack);
};

#endif // SPTSCRIPT_ANALYZER_H