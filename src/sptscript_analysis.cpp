// src/semantic_analyzer_visitor.cpp

// --- 包含必要的头文件 ---
#include "LangLexer.h"
#include "LangParser.h"                // ANTLR 生成的 Parser 类
#include "antlr4-runtime.h"            // ANTLR 运行时
#include "protocol_types.h"            // LSP 类型 (Diagnostic, Range)
#include "semantic_analyzer_visitor.h" // Visitor 类声明
#include "semantic_info.h"             // 类型、符号、作用域定义
#include "sptscript_analyzer.h"        // 主分析器类，用于导入处理
#include "uri.h"                       // Uri 类
#include <algorithm>                   // std::any_of, std::max
#include <iostream>                    // 用于调试输出 (可选)
#include <memory>                      // std::shared_ptr, std::make_shared
#include <plog/Log.h>                  // Plog 日志库
#include <stdexcept>                   // 标准异常
#include <unordered_set>
#include <vector> // std::vector

// --- 使用 ANTLR 命名空间 ---
using namespace antlr4;
using namespace antlr4::tree;

// --- 预定义的 TypeInfoPtr 常量 ---
static const TypeInfoPtr UnknownType = std::make_shared<BaseType>(BaseTypeKind::UNKNOWN);
static const TypeInfoPtr AnyType = std::make_shared<BaseType>(BaseTypeKind::ANY);
static const TypeInfoPtr VoidType = std::make_shared<BaseType>(BaseTypeKind::VOID);
static const TypeInfoPtr NullType = std::make_shared<BaseType>(BaseTypeKind::NULL_TYPE);
static const TypeInfoPtr IntType = std::make_shared<BaseType>(BaseTypeKind::INT);
static const TypeInfoPtr FloatType = std::make_shared<BaseType>(BaseTypeKind::FLOAT);
static const TypeInfoPtr NumberType = std::make_shared<BaseType>(BaseTypeKind::NUMBER);
static const TypeInfoPtr BoolType = std::make_shared<BaseType>(BaseTypeKind::BOOL);
static const TypeInfoPtr StringType = std::make_shared<BaseType>(BaseTypeKind::STRING);
static const TypeInfoPtr FunctionKeywordType = std::make_shared<BaseType>(BaseTypeKind::FUNCTION);
static const TypeInfoPtr CoroutineKeywordType = std::make_shared<BaseType>(BaseTypeKind::COROUTINE);

// --- 预定义常量结束 ---

// --- RAII 作用域管理助手 ---
namespace {
class ScopeGuard {
  SymbolTable &table;
  bool popped = false;
  ScopeKind kindForLog;
  antlr4::tree::ParseTree *ownerNodeForLog;

public:
  ScopeGuard(SymbolTable &st, ScopeKind kind, antlr4::tree::ParseTree *owner = nullptr)
      : table(st), kindForLog(kind), ownerNodeForLog(owner) {
    std::string ownerType = owner ? typeid(*owner).name() : "null";
    PLOGD << "进入作用域: 类型=" << static_cast<int>(kind) << ", 关联节点类型=" << ownerType;
    table.pushScope(kind, owner);
  }

  ~ScopeGuard() { pop(); }

  void pop() {
    if (!popped) {
      std::string ownerType = ownerNodeForLog ? typeid(*ownerNodeForLog).name() : "null";
      PLOGD << "退出作用域: 类型=" << static_cast<int>(kindForLog)
            << ", 关联节点类型=" << ownerType;
      table.popScope();
      popped = true;
    }
  }

  ScopeGuard(const ScopeGuard &) = delete;

  ScopeGuard &operator=(const ScopeGuard &) = delete;

  ScopeGuard(ScopeGuard &&other) noexcept
      : table(other.table), popped(other.popped), kindForLog(other.kindForLog),
        ownerNodeForLog(other.ownerNodeForLog) {
    other.popped = true;
  }

  ScopeGuard &operator=(ScopeGuard &&other) noexcept {
    if (this != &other) {
      pop();
      popped = other.popped;
      kindForLog = other.kindForLog;
      ownerNodeForLog = other.ownerNodeForLog;
      other.popped = true;
    }
    return *this;
  }
};
} // namespace

// 需要一个辅助函数来将 SymbolKind 转换为 CompletionItemKind
CompletionItemKind symbolToCompletionItemKind(SymbolKind kind) {
  switch (kind) {
  case SymbolKind::VARIABLE:
    return CompletionItemKind::Variable;
  case SymbolKind::PARAMETER:
    return CompletionItemKind::Variable; // LSP 通常用 Variable 代表参数
  case SymbolKind::FUNCTION:
    return CompletionItemKind::Function;
  case SymbolKind::CLASS:
    return CompletionItemKind::Class;
  case SymbolKind::MODULE:
    return CompletionItemKind::Module;
  case SymbolKind::TYPE_ALIAS:
    return CompletionItemKind::Interface; // 或 Struct/Keyword
  case SymbolKind::BUILTIN_FUNCTION:
    return CompletionItemKind::Function;
  case SymbolKind::BUILTIN_TYPE:
    return CompletionItemKind::Keyword; // 或 TypeParameter
  default:
    return CompletionItemKind::Text;
  }
}

// 辅助函数：将LSP的Position转换为文档中的字符偏移量 (需要DocumentManager)
// 注意：这个函数需要 DocumentManager 实例，你可能需要将其作为参数传递，
// 或者让 SptScriptAnalyzer 持有 DocumentManager 的引用/指针。
// 为简化，这里假设你可以直接访问它。
// 【重要】: 此函数是一个简化版，实际的 LSP 服务器通常有更健壮的方式处理行列和偏移转换，
//          并且通常由 LSP 客户端在 CompletionParams->context 中提供更精确的触发信息。
size_t positionToOffset(const std::string &content, Position pos) {
  size_t offset = 0;
  long currentLine = 0;
  long currentCharInLine = 0;
  for (char c : content) {
    if (currentLine == pos.line && currentCharInLine == pos.character) {
      return offset;
    }
    if (c == '\n') {
      currentLine++;
      currentCharInLine = 0;
    } else {
      currentCharInLine++;
    }
    offset++;
    if (currentLine > pos.line)
      break; // 避免不必要的遍历
  }
  // 如果位置超出文本范围，返回文本末尾或一个指示无效的值
  if (currentLine == pos.line && currentCharInLine == pos.character)
    return offset;
  return content.length(); // 或抛出异常，或返回特定错误码
}

// 获取光标前输入的标识符前缀 (简化版)
std::string getPrefixAtPosition(const std::string &content, Position position) {
  if (position.character == 0)
    return "";
  size_t endOffset = positionToOffset(content, position);
  size_t startOffset = endOffset;

  while (startOffset > 0) {
    char prevChar = content[startOffset - 1];
    if (std::isalnum(static_cast<unsigned char>(prevChar)) || prevChar == '_') {
      startOffset--;
    } else {
      break;
    }
  }
  if (endOffset > startOffset) {
    return content.substr(startOffset, endOffset - startOffset);
  }
  return "";
}

// --- SemanticAnalyzerVisitor 构造函数实现 ---
SemanticAnalyzerVisitor::SemanticAnalyzerVisitor(SymbolTable &table, std::vector<Diagnostic> &diags,
                                                 const Uri &uri, SptScriptAnalyzer &analyzerRef,
                                                 std::set<Uri> &visited,
                                                 AnalysisResult &resultRef) // ** 新增 resultRef **
    : symbolTable(table), diagnostics(diags), documentUri(uri), analyzer(analyzerRef),
      visitedInStack(visited), analysisResult(resultRef), // ** 初始化 analysisResult 引用 **
      currentClassType(nullptr), isInStaticMethod(false) {
  PLOGD << "SemanticAnalyzerVisitor 已创建，文档: " << documentUri.str();
  auto global = symbolTable.getGlobalScope();
  if (!global) {
    PLOGF << "严重错误: 无法获取全局作用域来定义内置符号!";
    return;
  }
  // 添加内置函数
  auto printSig = std::make_shared<FunctionSignature>();
  printSig->isVariadic = true;
  printSig->returnTypeInfo = VoidType;
  auto printSym = std::make_shared<SymbolInfo>("print", SymbolKind::BUILTIN_FUNCTION, printSig,
                                               Location{}, global);
  if (!symbolTable.defineSymbol(printSym))
    PLOGW << "警告：未能定义内置符号 'print'。";
  auto typeSig = std::make_shared<FunctionSignature>();
  typeSig->parameters.push_back({"value", AnyType});
  typeSig->returnTypeInfo = StringType;
  auto typeSym = std::make_shared<SymbolInfo>("type", SymbolKind::BUILTIN_FUNCTION, typeSig,
                                              Location{}, global);
  if (!symbolTable.defineSymbol(typeSym))
    PLOGW << "警告：未能定义内置符号 'type'。";
  auto ipairsSig = std::make_shared<FunctionSignature>();
  ipairsSig->parameters.push_back({"list_obj", std::make_shared<ListType>(AnyType)});
  ipairsSig->returnTypeInfo = AnyType;
  auto ipairsSym = std::make_shared<SymbolInfo>("ipairs", SymbolKind::BUILTIN_FUNCTION, ipairsSig,
                                                Location{}, global);
  if (!symbolTable.defineSymbol(ipairsSym))
    PLOGW << "警告：未能定义内置符号 'ipairs'。";
  auto pairsSig = std::make_shared<FunctionSignature>();
  pairsSig->parameters.push_back({"map_obj", std::make_shared<MapType>(AnyType, AnyType)});
  pairsSig->returnTypeInfo = AnyType;
  auto pairsSym = std::make_shared<SymbolInfo>("pairs", SymbolKind::BUILTIN_FUNCTION, pairsSig,
                                               Location{}, global);
  if (!symbolTable.defineSymbol(pairsSym))
    PLOGW << "警告：未能定义内置符号 'pairs'。";
  auto errorSig = std::make_shared<FunctionSignature>();
  errorSig->parameters.push_back({"message", StringType});
  errorSig->returnTypeInfo = VoidType;
  auto errorSym = std::make_shared<SymbolInfo>("error", SymbolKind::BUILTIN_FUNCTION, errorSig,
                                               Location{}, global);
  if (!symbolTable.defineSymbol(errorSym))
    PLOGW << "警告：未能定义内置符号 'error'。";
  auto assertSig = std::make_shared<FunctionSignature>();
  assertSig->parameters.push_back({"value", AnyType});
  assertSig->returnTypeInfo = VoidType;
  auto assertSym = std::make_shared<SymbolInfo>("assert", SymbolKind::BUILTIN_FUNCTION, assertSig,
                                                Location{}, global);
  if (!symbolTable.defineSymbol(assertSym))
    PLOGW << "警告：未能定义内置符号 'assert'。";
  auto tonumberSig = std::make_shared<FunctionSignature>();
  tonumberSig->parameters.push_back({"value", AnyType});
  tonumberSig->returnTypeInfo = NumberType;
  auto tonumberSym = std::make_shared<SymbolInfo>("tonumber", SymbolKind::BUILTIN_FUNCTION,
                                                  tonumberSig, Location{}, global);
  if (!symbolTable.defineSymbol(tonumberSym))
    PLOGW << "警告：未能定义内置符号 'tonumber'。";
  auto tostringSig = std::make_shared<FunctionSignature>();
  tostringSig->parameters.push_back({"value", AnyType});
  tostringSig->returnTypeInfo = StringType;
  auto tostringSym = std::make_shared<SymbolInfo>("tostring", SymbolKind::BUILTIN_FUNCTION,
                                                  tostringSig, Location{}, global);
  if (!symbolTable.defineSymbol(tostringSym))
    PLOGW << "警告：未能定义内置符号 'tostring'。";
  PLOGI << "SemanticAnalyzerVisitor 初始化完成，已添加部分内置符号。";
}

// --- 辅助函数实现 ---
void SemanticAnalyzerVisitor::recordScopeRange(std::shared_ptr<Scope> scope,
                                               antlr4::tree::ParseTree *ownerNode) {
  if (!scope || !ownerNode) {
    return;
  }
  // 尝试获取节点的 Range
  if (auto ruleCtx = dynamic_cast<antlr4::ParserRuleContext *>(ownerNode)) {
    Range scopeRange = getRange(ruleCtx);
    // 插入或更新范围到作用域的映射
    // 使用 insert_or_assign 可能更好，如果需要处理嵌套范围的更新逻辑
    analysisResult.scopeRangeMap[scopeRange] = scope;
    PLOGV << "记录作用域范围: [" << scopeRange.start.line << ":" << scopeRange.start.character
          << " - " << scopeRange.end.line << ":" << scopeRange.end.character << "] -> Scope Kind "
          << static_cast<int>(scope->kind);
  } else {
    PLOGW << "无法为 Scope Kind " << static_cast<int>(scope->kind)
          << " 获取节点范围，因为 ownerNode 不是 ParserRuleContext";
  }
}

/**
 * @brief 从 ANTLR 解析规则上下文获取 LSP Range。(修正)
 */
Range getRange(antlr4::ParserRuleContext *ctx) {
  if (!ctx)
    return Range{Position{0, 0}, Position{0, 1}};
  antlr4::Token *startToken = ctx->getStart();
  antlr4::Token *stopToken = ctx->getStop();
  if (!startToken)
    return Range{Position{0, 0}, Position{0, 1}};
  if (!stopToken)
    stopToken = startToken;
  return getTokenRange(startToken, stopToken); // 调用两个 token 的版本
}

/**
 * @brief 从 ANTLR 终端节点获取 LSP Range。(修正)
 */
Range getRange(antlr4::tree::TerminalNode *node) {
  if (!node)
    return Range{Position{0, 0}, Position{0, 1}};
  return getTokenRange(node->getSymbol()); // 调用单个 token 的版本
}

/**
 * @brief 从单个 ANTLR Token 获取 LSP Range。
 */
Range getTokenRange(antlr4::Token *token) {
  if (!token)
    return Range{Position{0, 0}, Position{0, 1}};
  long line = static_cast<long>(token->getLine()) - 1;
  long startChar = static_cast<long>(token->getCharPositionInLine());
  size_t tokenLength = token->getText().length();
  long endChar = startChar + static_cast<long>(std::max(static_cast<size_t>(1), tokenLength));
  line = std::max(0L, line);
  startChar = std::max(0L, startChar);
  endChar = std::max(startChar, endChar);
  return Range{Position{line, startChar}, Position{line, endChar}};
}

/**
 * @brief 从开始和结束 ANTLR Token 获取跨越两者的 LSP Range。(修正后的实现)
 */
Range getTokenRange(antlr4::Token *startToken, antlr4::Token *stopToken) {
  if (!startToken || !stopToken)
    return Range{Position{0, 0}, Position{0, 1}};
  long startLine = static_cast<long>(startToken->getLine()) - 1;
  long startChar = static_cast<long>(startToken->getCharPositionInLine());
  long stopLine = static_cast<long>(stopToken->getLine()) - 1;
  size_t stopTokenLength = stopToken->getText().length();
  long stopChar = static_cast<long>(stopToken->getCharPositionInLine()) +
                  static_cast<long>(std::max(static_cast<size_t>(1), stopTokenLength));
  startLine = std::max(0L, startLine);
  startChar = std::max(0L, startChar);
  stopLine = std::max(startLine, stopLine);
  if (stopLine == startLine) {
    stopChar = std::max(startChar, stopChar);
  } else {
    stopChar = std::max(0L, stopChar);
  }
  return Range{Position{startLine, startChar}, Position{stopLine, stopChar}};
}

/**
 * @brief 从 ANTLR 解析规则上下文获取 LSP Location。
 */
Location SemanticAnalyzerVisitor::getLocation(antlr4::ParserRuleContext *ctx) {
  return Location{documentUri.str(), getRange(ctx)};
}

/**
 * @brief 从 ANTLR 终端节点获取 LSP Location。
 */
Location SemanticAnalyzerVisitor::getLocation(antlr4::tree::TerminalNode *node) {
  return Location{documentUri.str(), getRange(node)};
}

/**
 * @brief 添加一条诊断信息 (错误或警告)。
 */
void SemanticAnalyzerVisitor::addDiagnostic(const Range &range, const std::string &message,
                                            DiagnosticSeverity severity) {
  Diagnostic diag;
  diag.range = range;
  diag.message = message;
  diag.severity = severity;
  diag.source = "semantic";
  diagnostics.push_back(std::move(diag));
  const char *severityStr = "Unknown";
  switch (severity) {
  case DiagnosticSeverity::Error:
    severityStr = "Error";
    break;
  case DiagnosticSeverity::Warning:
    severityStr = "Warning";
    break;
  case DiagnosticSeverity::Information:
    severityStr = "Info";
    break;
  case DiagnosticSeverity::Hint:
    severityStr = "Hint";
    break;
  }
  PLOGW << "语义诊断 [" << severityStr << "]: " << message << " at " << documentUri.path() << ":"
        << range.start.line + 1 << ":" << range.start.character + 1;
}

/**
 * @brief 从类型上下文 (TypeContext) 解析 TypeInfoPtr。
 */
// 在 semantic_analyzer_visitor.cpp 中
TypeInfoPtr SemanticAnalyzerVisitor::resolveTypeFromContext(LangParser::TypeContext *ctx) {
  if (!ctx) {
    PLOGW << "[ResolveType] 收到空的 TypeContext*";
    return UnknownType;
  }
  PLOGD << "[ResolveType] 解析类型上下文: " << ctx->getText();

  std::any result_any = defaultResult(); // 初始化

  // 根据 TypeContext 的具体子类型调用相应的 visit 方法
  // 这些 visit 方法都应该返回 std::any 包装的 TypeInfoPtr
  if (auto primCtx = dynamic_cast<LangParser::TypePrimitiveContext *>(ctx)) {
    result_any = visitTypePrimitive(primCtx);
  } else if (auto listCtx = dynamic_cast<LangParser::TypeListTypeContext *>(ctx)) {
    result_any = visitTypeListType(listCtx);
  } else if (auto mapCtx = dynamic_cast<LangParser::TypeMapContext *>(ctx)) {
    result_any = visitTypeMap(mapCtx);
  } else if (auto anyCtx = dynamic_cast<LangParser::TypeAnyContext *>(ctx)) {
    result_any = visitTypeAny(anyCtx);
  } else if (auto qIdCtx = dynamic_cast<LangParser::TypeQualifiedIdentifierContext *>(ctx)) {
    result_any = visitTypeQualifiedIdentifier(qIdCtx); // <--- 这个调用现在应该返回 TypeInfoPtr
  } else {
    PLOGE << "[ResolveType] 未知的 TypeContext 子类型 for: " << ctx->getText();
    addDiagnostic(getRange(ctx), "内部错误：无法识别的类型注解结构", DiagnosticSeverity::Error);
    return UnknownType;
  }

  try {
    auto resolvedType = std::any_cast<TypeInfoPtr>(result_any); // 现在这里应该能成功转换
    if (!resolvedType) {                                        // 检查是否为 nullptr
      PLOGE << "[ResolveType] visit 方法 for '" << ctx->getText()
            << "' 返回了空的 TypeInfoPtr (nullptr)。";
      addDiagnostic(getRange(ctx), "内部错误：无法解析类型注解 '" + ctx->getText() + "' (空类型)",
                    DiagnosticSeverity::Error);
      return UnknownType;
    }
    PLOGD << "[ResolveType] 成功解析得到类型: " << resolvedType->toString();
    return resolvedType;
  } catch (const std::bad_any_cast &e) {
    PLOGE << "[ResolveType] visit 方法 for '" << ctx->getText()
          << "' 返回的类型不是 TypeInfoPtr. Error: " << e.what()
          << ". 实际类型: " << result_any.type().name();
    addDiagnostic(getRange(ctx),
                  "内部错误：无法解析类型注解 '" + ctx->getText() + "' (类型转换失败)",
                  DiagnosticSeverity::Error);
    return UnknownType;
  } catch (const std::exception &e) {
    PLOGE << "[ResolveType] 解析类型时发生标准异常: " << e.what()
          << ". Context: " << ctx->getText();
    addDiagnostic(getRange(ctx), "内部错误：解析类型注解时发生错误。", DiagnosticSeverity::Error);
    return UnknownType;
  } catch (...) {
    PLOGE << "[ResolveType] 解析类型时发生未知异常." << ". Context: " << ctx->getText();
    addDiagnostic(getRange(ctx), "内部错误：解析类型注解时发生未知错误。",
                  DiagnosticSeverity::Error);
    return UnknownType;
  }
}

// --- Helper Function Placeholders / Partial Implementations ---

/**
 * @brief 处理导入语句。(修正：调用 analyzeDocument 时不传递 visitedInStack)
 */
std::shared_ptr<Scope> SemanticAnalyzerVisitor::processImport(const std::string &uriString,
                                                              antlr4::tree::ParseTree *importNode) {
  PLOGI << "处理导入: '" << uriString << "'";
  // 1. 解析 URI
  std::optional<Uri> resolvedUriOpt = analyzer.resolveImportPath(documentUri, uriString);
  if (!resolvedUriOpt || !resolvedUriOpt.value().isValid()) {
    addDiagnostic(getRange(dynamic_cast<ParserRuleContext *>(importNode)),
                  "无法解析或无效的导入路径: " + uriString, DiagnosticSeverity::Error);
    return nullptr;
  }
  Uri resolvedUri = resolvedUriOpt.value();

  // 2. 请求主分析器来分析（或获取缓存的）导入模块
  // *** 修正：调用 analyzeDocument 的公共接口，不传递 visitedInStack ***
  //    内部的 performAnalysis 会处理循环检测
  //    TODO: 需要确定被导入文件的版本号，这里暂时用 0 或 -1
  std::shared_ptr<const AnalysisResult> importedAnalysis =
      analyzer.analyzeDocument(resolvedUri, "", -1L, visitedInStack);

  if (!importedAnalysis) { // analyzeDocument 可能返回 nullptr 如果内部出错（如循环导入）
    addDiagnostic(getRange(dynamic_cast<ParserRuleContext *>(importNode)),
                  "无法分析导入的模块 (可能是循环导入或读取错误): " + uriString,
                  DiagnosticSeverity::Error);
    return nullptr;
  }
  if (!importedAnalysis->symbolTable) {
    addDiagnostic(getRange(dynamic_cast<ParserRuleContext *>(importNode)),
                  "导入的模块符号表无效: " + uriString, DiagnosticSeverity::Error);
    return nullptr;
  }

  // 这里的处理可能是个错误的决定，global作用域好像没什么用，都是tostring
  // print这些符号，而真正导入的符号在模块符号表children[0]中。
  //  3. 返回导入模块的全局作用域
  PLOGI << "导入 '" << uriString << "' 成功解析为 " << resolvedUri.str() << " 并分析完毕。";
  // 假设文件的顶层符号定义在全局作用域的第一个子作用域（即模块作用域）
  auto rootScopeForFile = importedAnalysis->symbolTable->getGlobalScope(); // 这是真正的全局
  if (rootScopeForFile && !rootScopeForFile->children.empty()) {
    // 假设第一个子作用域是该文件的模块作用域
    std::shared_ptr<Scope> moduleScopeOfImportedFile = rootScopeForFile->children[0];
    if (moduleScopeOfImportedFile && moduleScopeOfImportedFile->kind == ScopeKind::MODULE) {
      PLOGI << "[ProcessImport] 导入 '" << uriString << "' 成功。返回其模块作用域。";
      // --- 在这里添加调试日志，确认模块作用域的内容 ---
      SymbolInfoPtr playerInModule = moduleScopeOfImportedFile->resolveLocally("Player");
      if (playerInModule) {
        PLOGD << "[ProcessImport-Debug] 在 '" << resolvedUri.str()
              << "' 的模块作用域中找到 'Player'. Exported: " << playerInModule->isExported;
      } else {
        PLOGE << "[ProcessImport-Debug] 在 '" << resolvedUri.str()
              << "' 的模块作用域中未找到 'Player'!";
        PLOGD << "Symbols in " << resolvedUri.str() << " module scope:";
        for (const auto &pair : moduleScopeOfImportedFile->symbols) {
          PLOGD << "  - " << pair.first << " (Exported: " << pair.second->isExported << ")";
        }
      }
      // --- 结束调试日志 ---
      return moduleScopeOfImportedFile;
    } else {
      PLOGE << "[ProcessImport] 全局作用域的第一个子作用域不是模块作用域 for " << resolvedUri.str();
    }
  } else {
    PLOGE << "[ProcessImport] 导入的模块 " << resolvedUri.str()
          << " 的全局作用域没有子作用域（模块作用域未找到）。";
  }
  // 如果上述逻辑失败，回退或报错
  addDiagnostic(getRange(dynamic_cast<ParserRuleContext *>(importNode)),
                "无法找到导入模块 '" + uriString + "' 的有效模块作用域", DiagnosticSeverity::Error);
  return nullptr; // 或者返回 rootScopeForFile 依然让它失败，但错误更明显
}

/**
 * @brief 检查类型兼容性。
 */
bool SemanticAnalyzerVisitor::checkTypeAssignable(const TypeInfoPtr &source,
                                                  const TypeInfoPtr &target,
                                                  antlr4::tree::ParseTree *assignmentNode) {
  // (此函数实现保持与上一版本相同)
  if (!source || !target) {
    PLOGE << "类型检查错误: source 或 target 类型为 null。";
    return false;
  }
  PLOGV << "检查类型赋值: " << source->toString() << " -> " << target->toString();
  if (auto targetBase = std::dynamic_pointer_cast<BaseType>(target)) {
    if (targetBase->kind == BaseTypeKind::ANY) {
      PLOGV << " -> 允许: 目标是 'any'";
      return true;
    }
  }
  if (auto sourceBase = std::dynamic_pointer_cast<BaseType>(source)) {
    if (sourceBase->kind == BaseTypeKind::ANY) {
      bool targetIsAny = false;
      if (auto targetBaseCheck = std::dynamic_pointer_cast<BaseType>(target)) {
        targetIsAny = targetBaseCheck->kind == BaseTypeKind::ANY;
      }
      if (!targetIsAny && assignmentNode) {
        addDiagnostic(getRange(dynamic_cast<antlr4::ParserRuleContext *>(assignmentNode)),
                      "不能将 'any' 类型隐式赋给更具体的类型 '" + target->toString() + "'",
                      DiagnosticSeverity::Warning);
      }
      PLOGV << " -> " << (targetIsAny ? "允许" : "不允许") << ": 源是 'any'";
      return targetIsAny;
    }
  }
  if (source->isEqualTo(*target)) {
    PLOGV << " -> 允许: 类型相同";
    return true;
  }
  if (auto sourceBase = std::dynamic_pointer_cast<BaseType>(source)) {
    if (sourceBase->kind == BaseTypeKind::NULL_TYPE) {
      bool assignable = false;
      if (std::dynamic_pointer_cast<BaseType>(target) &&
          std::dynamic_pointer_cast<BaseType>(target)->kind == BaseTypeKind::ANY)
        assignable = true;
      else if (std::dynamic_pointer_cast<ListType>(target))
        assignable = true;
      else if (std::dynamic_pointer_cast<MapType>(target))
        assignable = true;
      else if (std::dynamic_pointer_cast<ClassType>(target))
        assignable = true;
      else if (auto targetUnion = std::dynamic_pointer_cast<UnionType>(target)) {
        assignable =
            std::any_of(targetUnion->memberTypes.begin(), targetUnion->memberTypes.end(),
                        [](const TypeInfoPtr &member) { return member->isEqualTo(*NullType); });
      }
      if (!assignable && assignmentNode) {
        addDiagnostic(getRange(dynamic_cast<antlr4::ParserRuleContext *>(assignmentNode)),
                      "无法将 'null' 赋给类型 '" + target->toString() + "'",
                      DiagnosticSeverity::Error);
      }
      PLOGV << " -> " << (assignable ? "允许" : "不允许") << ": 源是 'null'";
      return assignable;
    }
  }
  auto sourceBaseNum = std::dynamic_pointer_cast<BaseType>(source);
  auto targetBaseNum = std::dynamic_pointer_cast<BaseType>(target);
  if (sourceBaseNum && targetBaseNum) {
    if ((sourceBaseNum->kind == BaseTypeKind::INT && targetBaseNum->kind == BaseTypeKind::FLOAT) ||
        ((sourceBaseNum->kind == BaseTypeKind::INT || sourceBaseNum->kind == BaseTypeKind::FLOAT) &&
         targetBaseNum->kind == BaseTypeKind::NUMBER)) {
      PLOGV << " -> 允许: 数字类型提升/兼容";
      return true;
    }
  }
  if (auto targetUnion = std::dynamic_pointer_cast<UnionType>(target)) {
    for (const auto &memberType : targetUnion->memberTypes) {
      if (checkTypeAssignable(source, memberType, nullptr)) {
        PLOGV << " -> 允许: 源类型可赋给联合体成员 " << memberType->toString();
        return true;
      }
    }
    if (assignmentNode) {
      addDiagnostic(getRange(dynamic_cast<antlr4::ParserRuleContext *>(assignmentNode)),
                    "类型 '" + source->toString() + "' 不能赋给联合类型 '" + target->toString() +
                        "'",
                    DiagnosticSeverity::Error);
    }
    PLOGV << " -> 不允许: 源类型无法赋给联合体的任何成员";
    return false;
  }
  if (auto sourceUnion = std::dynamic_pointer_cast<UnionType>(source)) {
    bool allAssignable = true;
    for (const auto &memberType : sourceUnion->memberTypes) {
      if (!checkTypeAssignable(memberType, target, nullptr)) {
        allAssignable = false;
        break;
      }
    }
    if (!allAssignable && assignmentNode) {
      addDiagnostic(getRange(dynamic_cast<antlr4::ParserRuleContext *>(assignmentNode)),
                    "联合类型 '" + source->toString() + "' 不能赋给类型 '" + target->toString() +
                        "' (并非所有成员都可赋值)",
                    DiagnosticSeverity::Error);
    }
    PLOGV << " -> " << (allAssignable ? "允许" : "不允许") << ": 源是联合体";
    return allAssignable;
  }
  if (auto sourceList = std::dynamic_pointer_cast<ListType>(source)) {
    if (auto targetList = std::dynamic_pointer_cast<ListType>(target)) {
      bool elementsAssignable =
          checkTypeAssignable(sourceList->elementType, targetList->elementType, nullptr);
      if (!elementsAssignable && assignmentNode) {
        addDiagnostic(getRange(dynamic_cast<antlr4::ParserRuleContext *>(assignmentNode)),
                      "无法赋值：列表元素类型不兼容 '" + sourceList->elementType->toString() +
                          "' -> '" + targetList->elementType->toString() + "'",
                      DiagnosticSeverity::Error);
      }
      PLOGV << " -> " << (elementsAssignable ? "允许" : "不允许") << ": 列表类型";
      return elementsAssignable;
    }
  }
  if (auto sourceMap = std::dynamic_pointer_cast<MapType>(source)) {
    if (auto targetMap = std::dynamic_pointer_cast<MapType>(target)) {
      bool keysAssignable = checkTypeAssignable(sourceMap->keyType, targetMap->keyType, nullptr);
      bool valuesAssignable =
          checkTypeAssignable(sourceMap->valueType, targetMap->valueType, nullptr);
      bool mapsAssignable = keysAssignable && valuesAssignable;
      if (!mapsAssignable && assignmentNode) {
        addDiagnostic(getRange(dynamic_cast<antlr4::ParserRuleContext *>(assignmentNode)),
                      "无法赋值：映射类型不兼容。键: '" + sourceMap->keyType->toString() +
                          "' -> '" + targetMap->keyType->toString() + "', 值: '" +
                          sourceMap->valueType->toString() + "' -> '" +
                          targetMap->valueType->toString() + "'",
                      DiagnosticSeverity::Error);
      }
      PLOGV << " -> " << (mapsAssignable ? "允许" : "不允许") << ": 映射类型";
      return mapsAssignable;
    }
  }
  if (auto sourceTuple = std::dynamic_pointer_cast<TupleType>(source)) {
    if (auto targetList = std::dynamic_pointer_cast<ListType>(target)) {
      if (targetList->elementType->isEqualTo(*AnyType)) {
        PLOGV << " -> 允许: 元组赋给 list<any>";
        return true;
      }
      bool allElementsAssignable = true;
      for (const auto &elemType : sourceTuple->elementTypes) {
        if (!checkTypeAssignable(elemType, targetList->elementType, nullptr)) {
          allElementsAssignable = false;
          break;
        }
      }
      if (!allElementsAssignable && assignmentNode) {
        addDiagnostic(getRange(dynamic_cast<antlr4::ParserRuleContext *>(assignmentNode)),
                      "无法赋值：并非所有元组元素类型 '" + source->toString() +
                          "' 都能赋给列表元素类型 '" + targetList->elementType->toString() + "'",
                      DiagnosticSeverity::Error);
      }
      PLOGV << " -> " << (allElementsAssignable ? "允许" : "不允许") << ": 元组赋给 list<T>";
      return allElementsAssignable;
    }
    if (auto targetTuple = std::dynamic_pointer_cast<TupleType>(target)) {
      if (sourceTuple->elementTypes.size() != targetTuple->elementTypes.size()) {
        if (assignmentNode)
          addDiagnostic(getRange(dynamic_cast<antlr4::ParserRuleContext *>(assignmentNode)),
                        "无法赋值：元组元素数量不匹配", DiagnosticSeverity::Error);
        PLOGV << " -> 不允许: 元组元素数量不匹配";
        return false;
      }
      bool structureMatch = true;
      for (size_t i = 0; i < sourceTuple->elementTypes.size(); ++i) {
        if (!checkTypeAssignable(sourceTuple->elementTypes[i], targetTuple->elementTypes[i],
                                 nullptr)) {
          structureMatch = false;
          break;
        }
      }
      if (!structureMatch && assignmentNode) {
        addDiagnostic(getRange(dynamic_cast<antlr4::ParserRuleContext *>(assignmentNode)),
                      "无法赋值：元组类型结构不兼容 '" + source->toString() + "' -> '" +
                          target->toString() + "'",
                      DiagnosticSeverity::Error);
      }
      PLOGV << " -> " << (structureMatch ? "允许" : "不允许") << ": 元组赋给元组";
      return structureMatch;
    }
  }
  if (std::dynamic_pointer_cast<FunctionSignature>(source) &&
      std::dynamic_pointer_cast<FunctionSignature>(target)) {
    bool same = source->isEqualTo(*target);
    if (!same && assignmentNode) {
      addDiagnostic(getRange(dynamic_cast<antlr4::ParserRuleContext *>(assignmentNode)),
                    "函数签名不兼容", DiagnosticSeverity::Error);
    }
    PLOGV << " -> " << (same ? "允许" : "不允许") << ": 函数签名比较 (简化)";
    return same;
  }
  if (std::dynamic_pointer_cast<FunctionSignature>(source) &&
      target->isEqualTo(*FunctionKeywordType)) {
    PLOGV << " -> 允许: 函数赋给 'function' 关键字类型";
    return true;
  }
  if (assignmentNode) {
    addDiagnostic(getRange(dynamic_cast<antlr4::ParserRuleContext *>(assignmentNode)),
                  "类型不兼容，无法将 '" + source->toString() + "' 赋给 '" + target->toString() +
                      "'",
                  DiagnosticSeverity::Error);
  }
  PLOGV << " -> 不允许: 类型不兼容 (默认)";
  return false;
}

/**
 * @brief 获取已访问节点的推断类型。
 */
TypeInfoPtr SemanticAnalyzerVisitor::getTypeFromNode(antlr4::tree::ParseTree *node) {
  if (!node)
    return UnknownType;
  PLOGV << "查找节点类型 (未实现，返回 Unknown): " << node->getText();
  return UnknownType;
}

// --- 基础类型和字面量 Visit 方法 ---
std::any SemanticAnalyzerVisitor::visitTypePrimitive(LangParser::TypePrimitiveContext *ctx) {
  return visit(ctx->primitiveType());
}

std::any SemanticAnalyzerVisitor::visitTypeAny(LangParser::TypeAnyContext *ctx) {
  (void)ctx;
  return std::any(AnyType);
}

std::any SemanticAnalyzerVisitor::visitPrimitiveType(LangParser::PrimitiveTypeContext *ctx) {
  if (ctx->INT())
    return std::any(IntType);
  if (ctx->FLOAT())
    return std::any(FloatType);
  if (ctx->NUMBER())
    return std::any(NumberType);
  if (ctx->STRING())
    return std::any(StringType);
  if (ctx->BOOL())
    return std::any(BoolType);
  if (ctx->VOID())
    return std::any(VoidType);
  if (ctx->NULL_())
    return std::any(NullType);
  if (ctx->FUNCTION())
    return std::any(FunctionKeywordType);
  if (ctx->COROUTINE())
    return std::any(CoroutineKeywordType);
  PLOGW << "未知的基本类型: " << ctx->getText();
  addDiagnostic(getRange(ctx), "无法识别的基本类型关键字: " + ctx->getText());
  return std::any(UnknownType);
}

std::any SemanticAnalyzerVisitor::visitTypeListType(LangParser::TypeListTypeContext *ctx) {
  return visit(ctx->listType());
}

std::any SemanticAnalyzerVisitor::visitTypeMap(LangParser::TypeMapContext *ctx) {
  return visit(ctx->mapType());
}

std::any SemanticAnalyzerVisitor::visitListType(LangParser::ListTypeContext *ctx) {
  TypeInfoPtr elementType = AnyType;
  if (ctx->type()) {
    elementType = resolveTypeFromContext(ctx->type());
    if (!elementType || elementType == UnknownType) {
      addDiagnostic(getRange(ctx->type()), "列表元素类型无效或未知，假定为 'any'",
                    DiagnosticSeverity::Warning);
      elementType = AnyType;
    }
  }
  return std::any(std::make_shared<ListType>(elementType));
}

std::any SemanticAnalyzerVisitor::visitMapType(LangParser::MapTypeContext *ctx) {
  TypeInfoPtr keyType = AnyType;
  TypeInfoPtr valueType = AnyType;
  if (ctx->type().size() == 2) {
    keyType = resolveTypeFromContext(ctx->type(0));
    valueType = resolveTypeFromContext(ctx->type(1));
    if (!keyType || keyType == UnknownType) {
      addDiagnostic(getRange(ctx->type(0)), "映射键类型无效或未知，假定为 'any'",
                    DiagnosticSeverity::Warning);
      keyType = AnyType;
    }
    if (!valueType || valueType == UnknownType) {
      addDiagnostic(getRange(ctx->type(1)), "映射值类型无效或未知，假定为 'any'",
                    DiagnosticSeverity::Warning);
      valueType = AnyType;
    }
    if (keyType->isEqualTo(*NullType)) {
      addDiagnostic(getRange(ctx->type(0)), "映射的键类型不能是 'null'", DiagnosticSeverity::Error);
      keyType = AnyType;
    }
  } else if (!ctx->type().empty()) {
    addDiagnostic(getRange(ctx), "内部错误：map 类型参数数量应为 0 或 2",
                  DiagnosticSeverity::Error);
  }
  return std::any(std::make_shared<MapType>(keyType, valueType));
}

std::any SemanticAnalyzerVisitor::visitAtomexp(LangParser::AtomexpContext *ctx) {
  if (ctx->NULL_())
    return std::any(NullType);
  if (ctx->TRUE() || ctx->FALSE())
    return std::any(BoolType);
  if (ctx->INTEGER())
    return std::any(IntType);
  if (ctx->FLOAT_LITERAL())
    return std::any(FloatType);
  if (ctx->STRING_LITERAL())
    return std::any(StringType);
  PLOGW << "无法识别的原子表达式: " << ctx->getText();
  addDiagnostic(getRange(ctx), "未知的原子字面量", DiagnosticSeverity::Error);
  return std::any(UnknownType);
}

// --- 其他基础 Visit 方法 ---
std::any SemanticAnalyzerVisitor::visitCompilationUnit(LangParser::CompilationUnitContext *ctx) {
  PLOGD << "visitCompilationUnit: " << documentUri.str();
  ScopeGuard moduleScopeGuard(symbolTable, ScopeKind::MODULE, ctx);
  return visitChildren(ctx);
}

std::any SemanticAnalyzerVisitor::visitBlockStatement(LangParser::BlockStatementContext *ctx) {
  PLOGV << "visitBlockStatement";
  ScopeGuard blockScopeGuard(symbolTable, ScopeKind::BLOCK, ctx);
  // 记录作用域范围 ---
  recordScopeRange(symbolTable.getCurrentScope(), ctx);
  return visitChildren(ctx);
}

std::any SemanticAnalyzerVisitor::visitBlockStmt(LangParser::BlockStmtContext *ctx) {
  return visit(ctx->blockStatement());
}

std::any SemanticAnalyzerVisitor::visitSemicolonStmt(LangParser::SemicolonStmtContext *ctx) {
  (void)ctx;
  PLOGV << "visitSemicolonStmt";
  return std::any();
}

std::any
SemanticAnalyzerVisitor::visitQualifiedIdentifier(LangParser::QualifiedIdentifierContext *ctx) {
  std::string qualifiedName = "";
  for (size_t i = 0; i < ctx->IDENTIFIER().size(); ++i) {
    if (i > 0) {
      qualifiedName += ".";
    }
    qualifiedName += ctx->IDENTIFIER(i)->getText();
  }
  PLOGV << "visitQualifiedIdentifier: " << qualifiedName;
  return std::any(qualifiedName);
}

// --- 结束 Part 1 (修正后) ---

// src/semantic_analyzer_visitor.cpp

// --- Part 1 code from previous response should be here ---
// ... (构造函数, helper 函数, 基础 visit 方法等) ...

// ============================================================================
// Part 2: 声明和作用域管理 (Declarations and Scope Management)
// ============================================================================

// --- 处理声明语句的入口 ---

/**
 * @brief 访问声明语句 (DeclarationStmtContext)。
 * 这是 statement 规则的一个分支，指向一个 declaration。
 */
std::any SemanticAnalyzerVisitor::visitDeclarationStmt(LangParser::DeclarationStmtContext *ctx) {
  PLOGV << "visitDeclarationStmt";
  // 直接访问其包含的 declaration 节点
  return visit(ctx->declaration());
}

/**
 * @brief 访问声明 (DeclarationContext)。
 * 处理可选的 'export' 关键字，并分发到具体的声明类型 (变量、函数、类)。
 */
std::any SemanticAnalyzerVisitor::visitDeclaration(LangParser::DeclarationContext *ctx) {
  bool isExported = (ctx->EXPORT() != nullptr); // 检查是否有 export 关键字
  PLOGV << "visitDeclaration" << (isExported ? " (Exported)" : "");

  std::any declarationResult;            // 用于存储具体声明 visit 方法的返回值
  SymbolInfoPtr definedSymbol = nullptr; // 指向由具体声明创建的符号

  // 根据具体的声明类型调用相应的 visit 方法
  if (ctx->variableDeclaration()) {
    declarationResult = visit(ctx->variableDeclaration());
    // 尝试获取变量声明返回的符号列表（虽然这里通常只有一个）
    try {
      // VariableDeclaration 可能返回 vector<SymbolInfoPtr>
      auto symbols = std::any_cast<std::vector<SymbolInfoPtr>>(declarationResult);
      if (!symbols.empty()) {
        definedSymbol = symbols.front(); // 假设我们只关心第一个（或唯一的）符号
      }
    } catch (const std::bad_any_cast &) { /* 忽略转换失败 */
    } catch (const std::exception &e) {
      PLOGE << "访问 variableDeclaration 结果时出错: " << e.what();
    }
  } else if (ctx->functionDeclaration()) {
    declarationResult = visit(ctx->functionDeclaration());
    // 尝试获取函数声明返回的符号
    try {
      definedSymbol = std::any_cast<SymbolInfoPtr>(declarationResult);
    } catch (const std::bad_any_cast &) { /* 忽略 */
    } catch (const std::exception &e) {
      PLOGE << "访问 functionDeclaration 结果时出错: " << e.what();
    }
  } else if (ctx->classDeclaration()) {
    declarationResult = visit(ctx->classDeclaration());
    // 尝试获取类声明返回的符号
    try {
      definedSymbol = std::any_cast<SymbolInfoPtr>(declarationResult);
    } catch (const std::bad_any_cast &) { /* 忽略 */
    } catch (const std::exception &e) {
      PLOGE << "访问 classDeclaration 结果时出错: " << e.what();
    }
  } else {
    PLOGW << "空的声明节点?"; // 理论上不应发生，除非语法允许
    addDiagnostic(getRange(ctx), "无效的空声明", DiagnosticSeverity::Error);
    return std::any();
  }

  // 如果是导出的，并且成功获取了符号，标记它
  if (definedSymbol && isExported) {
    PLOGD << "标记符号 '" << definedSymbol->name << "' 为 exported";
    definedSymbol->isExported = true;
  } else if (isExported && !definedSymbol) {
    PLOGW << "export 关键字后似乎没有成功定义的符号";
    // 可能不需要报错，因为错误可能在子 visit 方法中已报告
  }

  // 返回具体声明 visit 方法的结果（通常是 SymbolInfoPtr 或 vector<SymbolInfoPtr>）
  return declarationResult;
}

// --- 变量声明 ---

/**
 * @brief 访问变量声明定义 (VariableDeclarationDefContext)。
 * 处理 `[global?] [const?] (type|auto) ID [ = expression ] ;`
 * @return std::any 包含一个 std::vector<SymbolInfoPtr>，即使只有一个变量。
 */
std::any SemanticAnalyzerVisitor::visitVariableDeclarationDef(
    LangParser::VariableDeclarationDefContext *ctx) {
  bool isGlobal = (ctx->GLOBAL() != nullptr);
  bool isConst = (ctx->CONST() != nullptr);
  PLOGD << "visitVariableDeclarationDef" << (isGlobal ? " (Global)" : "")
        << (isConst ? " (Const)" : "");

  std::vector<SymbolInfoPtr> definedSymbols; // 用于存储此声明定义的符号

  // 1. 获取标识符名称
  antlr4::tree::TerminalNode *identifierNode = ctx->declaration_item()->IDENTIFIER();
  if (!identifierNode) {
    PLOGW << "空的标识符?"; // 理论上不应发生，除非语法允许
    addDiagnostic(getRange(ctx), "无效的空标识符", DiagnosticSeverity::Error);
    return std::any();
  }
  std::string varName = identifierNode->toString();
  Location defLoc = getLocation(identifierNode);

  // 2. 确定变量类型
  TypeInfoPtr varType = UnknownType;
  if (ctx->declaration_item()->type()) { // 显式类型注解
    varType = resolveTypeFromContext(ctx->declaration_item()->type());
    if (!varType || varType == UnknownType) {
      addDiagnostic(getRange(ctx->declaration_item()->type()), "无效的变量类型注解",
                    DiagnosticSeverity::Error);
      varType = UnknownType; // 保持未知
    }
    // 检查是否使用 void 声明变量
    if (varType->isEqualTo(*VoidType)) {
      addDiagnostic(getRange(ctx->declaration_item()->type()),
                    "不能声明 void 类型的变量 '" + varName + "'", DiagnosticSeverity::Error);
      varType = UnknownType; // 将类型重置为未知
    }
  } else if (ctx->declaration_item()->AUTO()) { // 使用 auto
    if (ctx->expression()) {                    // 必须有初始化表达式才能推断
      // 推断初始化表达式的类型
      varType = inferExpressionType(ctx->expression());
      if (!varType || varType == UnknownType) {
        addDiagnostic(getRange(ctx->expression()),
                      "无法从初始化表达式推断 'auto' 变量 '" + varName + "' 的类型",
                      DiagnosticSeverity::Warning);
        varType = AnyType; // 推断失败，默认为 any？或者保持 Unknown？根据语言策略决定
      } else if (varType->isEqualTo(*VoidType)) {
        addDiagnostic(getRange(ctx->expression()),
                      "不能用返回 void 的表达式初始化 'auto' 变量 '" + varName + "'",
                      DiagnosticSeverity::Error);
        varType = UnknownType; // 推断为 void 是错误的
      }
      PLOGV << "变量 '" << varName << "' 使用 auto 推断类型为: " << varType->toString();
    } else {
      addDiagnostic(getRange(ctx->declaration_item()->AUTO()),
                    "'auto' 变量 '" + varName + "' 必须被初始化", DiagnosticSeverity::Error);
      varType = UnknownType; // 没有初始化器，无法推断
    }
  } else {
    // 语法应该保证 type 或 auto 存在，但作为防御性编程添加检查
    addDiagnostic(getRange(ctx->declaration_item()), "内部错误：变量声明缺少类型或 auto",
                  DiagnosticSeverity::Error);
    varType = UnknownType;
  }

  // 3. 创建符号信息
  SymbolInfoPtr varSymbol = std::make_shared<SymbolInfo>(
      varName, SymbolKind::VARIABLE, varType, defLoc, symbolTable.getCurrentScope(), ctx);
  varSymbol->isConst = isConst;
  varSymbol->isGlobal = isGlobal; // 标记是否由 global 关键字声明

  // 4. 在符号表中定义符号
  // 对于 global 变量，直接在全局作用域定义
  // 对于非 global 变量，在当前作用域定义
  bool defined;
  if (isGlobal) {
    // 将符号的作用域显式设置为全局作用域
    varSymbol->scope = symbolTable.getGlobalScope(); // 更新弱引用
    // 在全局作用域尝试定义
    defined = symbolTable.getGlobalScope()->define(varSymbol);
    PLOGV << "尝试在全局作用域定义符号 '" << varName << "'";
  } else {
    // 在当前作用域定义
    defined = symbolTable.defineSymbol(varSymbol);
    PLOGV << "尝试在当前作用域定义符号 '" << varName << "'";
  }

  if (!defined) {
    // 获取已存在符号的信息，提供更详细的错误
    SymbolInfoPtr existingSymbol = isGlobal
                                       ? symbolTable.getGlobalScope()->resolveLocally(varName)
                                       : symbolTable.getCurrentScope()->resolveLocally(varName);
    std::string errorMsg = "符号 '" + varName + "' 在";
    errorMsg += (isGlobal ? "全局" : "当前");
    errorMsg += "作用域中重复定义";
    if (existingSymbol) {
      errorMsg += "。先前定义在 " +
                  std::to_string(existingSymbol->definitionLocation.range.start.line + 1) + ":" +
                  std::to_string(existingSymbol->definitionLocation.range.start.character + 1);
    }
    addDiagnostic(getRange(ctx->declaration_item()->IDENTIFIER()), errorMsg,
                  DiagnosticSeverity::Error);
  } else {
    definedSymbols.push_back(varSymbol); // 添加到成功定义的列表
    analysisResult.nodeSymbolMap[identifierNode] = varSymbol;
    PLOGD << "成功定义符号 '" << varName << "' 类型: " << varType->toString();
  }

  // 5. 处理初始化表达式 (如果存在)
  if (ctx->expression()) {
    TypeInfoPtr initExprType = inferExpressionType(ctx->expression());
    if (!initExprType || initExprType == UnknownType) {
      addDiagnostic(getRange(ctx->expression()), "无法确定初始化表达式的类型",
                    DiagnosticSeverity::Warning);
    } else {
      // 检查初始化表达式类型是否可以赋给变量类型
      if (varType != UnknownType &&
          !checkTypeAssignable(initExprType, varType, ctx->expression())) {
        // checkTypeAssignable 内部会添加诊断信息
        PLOGW << "类型不匹配：无法将初始化表达式类型 '" << initExprType->toString()
              << "' 赋给变量 '" << varName << "' 类型 '" << varType->toString() << "'";
      }
      // 如果变量是 auto，并且初始化成功推断，更新 varType (虽然前面已经做过一次)
      if (ctx->declaration_item()->AUTO() && varType != initExprType && varType != UnknownType &&
          initExprType != UnknownType) {
        PLOGV << "更新 auto 变量 '" << varName << "' 类型从推断的 " << varType->toString()
              << " 到初始值类型 " << initExprType->toString();
        varType = initExprType;
        if (varSymbol)
          varSymbol->type = varType; // 更新符号表中的类型
      }
    }
  } else if (isConst) {
    // const 变量必须初始化
    addDiagnostic(getRange(ctx->declaration_item()->IDENTIFIER()),
                  "'const' 变量 '" + varName + "' 必须被初始化", DiagnosticSeverity::Error);
  }

  // 返回包含已定义符号（可能为空）的向量
  return std::any(definedSymbols);
}

// --- 函数和参数声明 ---

/**
 * @brief 访问函数声明定义 (FunctionDeclarationDefContext)。
 * 处理 `[global?] type qualifiedIdentifier ( [paramList] ) blockStatement`
 * @return std::any 包含函数符号的 SymbolInfoPtr。
 */
std::any SemanticAnalyzerVisitor::visitFunctionDeclarationDef(
    LangParser::FunctionDeclarationDefContext *ctx) {
  bool isGlobal = (ctx->GLOBAL() != nullptr);
  antlr4::tree::ParseTree *nameNode = ctx->qualifiedIdentifier(); // 获取名称节点
  std::string funcName;
  // 获取函数名 (可能是限定名)
  try {
    funcName = std::any_cast<std::string>(visit(ctx->qualifiedIdentifier()));
  } catch (const std::bad_any_cast &) {
    addDiagnostic(getRange(ctx->qualifiedIdentifier()), "内部错误：无法获取函数名",
                  DiagnosticSeverity::Error);
    return std::any(); // 无法继续
  }
  PLOGD << "visitFunctionDeclarationDef: " << funcName << (isGlobal ? " (Global)" : "");

  Location defLoc = getLocation(ctx->qualifiedIdentifier()); // 函数名定义位置

  // 1. 解析返回类型
  TypeInfoPtr returnType = resolveTypeFromContext(ctx->type());
  if (!returnType || returnType == UnknownType) {
    addDiagnostic(getRange(ctx->type()), "函数 '" + funcName + "' 的返回类型无效或未知",
                  DiagnosticSeverity::Warning);
    returnType = UnknownType; // 或 AnyType?
  }

  // 2. 创建函数签名和符号
  auto signature = std::make_shared<FunctionSignature>();
  signature->returnTypeInfo = returnType; // 设置返回类型

  // 提前创建函数符号，以便在函数体内可以递归调用（如果需要）
  SymbolInfoPtr funcSymbol = std::make_shared<SymbolInfo>(
      funcName, SymbolKind::FUNCTION, signature, defLoc,
      isGlobal ? symbolTable.getGlobalScope() : symbolTable.getCurrentScope(), ctx);
  funcSymbol->isGlobal = isGlobal;

  // 3. 在当前（或全局）作用域定义函数符号
  bool defined;
  if (isGlobal) {
    funcSymbol->scope = symbolTable.getGlobalScope();
    defined = symbolTable.getGlobalScope()->define(funcSymbol);
    PLOGV << "尝试在全局作用域定义函数 '" << funcName << "'";
  } else {
    defined = symbolTable.defineSymbol(funcSymbol); // defineSymbol 内部会设置 scope
    PLOGV << "尝试在当前作用域定义函数 '" << funcName << "'";
  }

  if (!defined) {
    SymbolInfoPtr existingSymbol = isGlobal
                                       ? symbolTable.getGlobalScope()->resolveLocally(funcName)
                                       : symbolTable.getCurrentScope()->resolveLocally(funcName);
    std::string errorMsg = "函数 '" + funcName + "' 在";
    errorMsg += (isGlobal ? "全局" : "当前");
    errorMsg += "作用域中重复定义";
    if (existingSymbol) {
      errorMsg +=
          "。先前定义在 " + std::to_string(existingSymbol->definitionLocation.range.start.line + 1);
    }
    addDiagnostic(getRange(ctx->qualifiedIdentifier()), errorMsg, DiagnosticSeverity::Error);
    // 即使重定义，仍然继续处理函数体，以便发现内部错误
  } else {
    PLOGD << "成功定义函数符号 '" << funcName << "'";
    // 如果 qualifiedIdentifier 返回的是最内层的 IDENTIFIER 节点更好，
    // 否则，映射整个 qualifiedIdentifier 节点也可以。
    // 假设我们需要映射到名字本身所在的节点。
    if (auto qIdCtx = dynamic_cast<LangParser::QualifiedIdentifierContext *>(nameNode)) {
      if (!qIdCtx->IDENTIFIER().empty()) {
        // 映射最后一个 IDENTIFIER (简单名称部分)
        analysisResult.nodeSymbolMap[qIdCtx->IDENTIFIER().back()] = funcSymbol;
      }
    }
    // analysisResult.nodeSymbolMap[nameNode] = funcSymbol; // 映射整个限定名节点
  }

  // 4. 创建函数作用域
  ScopeGuard funcScopeGuard(symbolTable, ScopeKind::FUNCTION, ctx);
  recordScopeRange(symbolTable.getCurrentScope(), ctx);
  // 5. 将预期返回类型压栈 (用于检查 return 语句)
  expectedReturnTypeStack.push(returnType);
  PLOGV << "压入预期返回类型: " << (returnType ? returnType->toString() : "null");

  // 6. 访问参数列表，在函数作用域内定义参数符号
  bool isVariadic = false;
  if (ctx->parameterList()) {
    std::any paramResult = visit(ctx->parameterList());
    try {
      // parameterList 返回一个 pair: {vector<param_symbols>, is_variadic}
      auto paramsPair = std::any_cast<std::pair<std::vector<SymbolInfoPtr>, bool>>(paramResult);
      isVariadic = paramsPair.second;
      // 将参数类型添加到函数签名中
      for (const auto &paramSymbol : paramsPair.first) {
        if (paramSymbol && paramSymbol->type) {
          signature->parameters.push_back({paramSymbol->name, paramSymbol->type});
        } else {
          PLOGW << "函数 '" << funcName << "' 的参数处理时遇到无效符号或类型";
        }
      }
    } catch (const std::bad_any_cast &e) {
      PLOGE << "内部错误: visitParameterList 返回类型不匹配. Error: " << e.what();
      addDiagnostic(getRange(ctx->parameterList()), "内部错误：处理参数列表失败",
                    DiagnosticSeverity::Error);
    } catch (const std::exception &e) {
      PLOGE << "访问 parameterList 结果时出错: " << e.what();
    }
  }
  signature->isVariadic = isVariadic; // 设置可变参数标志

  // 7. 访问函数体 (块语句)
  visit(ctx->blockStatement());

  // 8. 弹出预期返回类型
  if (!expectedReturnTypeStack.empty()) {
    PLOGV << "弹出预期返回类型";
    expectedReturnTypeStack.pop();
  } else {
    PLOGE << "内部错误：尝试弹出空的预期返回类型栈";
  }

  // ScopeGuard 在析构时会自动弹出函数作用域

  return std::any(funcSymbol); // 返回函数符号信息
}

/**
 * @brief 访问多返回值函数声明 (MultiReturnFunctionDeclarationDefContext)。
 * 处理 `[global?] mutivar qualifiedIdentifier ( [paramList] ) blockStatement`
 * @return std::any 包含函数符号的 SymbolInfoPtr。
 */
std::any SemanticAnalyzerVisitor::visitMultiReturnFunctionDeclarationDef(
    LangParser::MultiReturnFunctionDeclarationDefContext *ctx) {
  bool isGlobal = (ctx->GLOBAL() != nullptr);
  std::string funcName;
  antlr4::tree::ParseTree *nameNode = ctx->qualifiedIdentifier(); // 获取名称节点

  try {
    funcName = std::any_cast<std::string>(visit(ctx->qualifiedIdentifier()));
  } catch (...) {
    addDiagnostic(getRange(ctx->qualifiedIdentifier()), "内部错误：无法获取多返回值函数名",
                  DiagnosticSeverity::Error);
    return std::any();
  }
  PLOGD << "visitMultiReturnFunctionDeclarationDef: " << funcName << (isGlobal ? " (Global)" : "");

  Location defLoc = getLocation(ctx->qualifiedIdentifier());

  // 1. 创建函数签名 (返回类型为 MultiReturnTag)
  auto signature = std::make_shared<FunctionSignature>();
  signature->returnTypeInfo = FunctionSignature::MultiReturnTag{};

  // 2. 创建并定义函数符号 (与单返回值函数类似)
  SymbolInfoPtr funcSymbol = std::make_shared<SymbolInfo>(
      funcName, SymbolKind::FUNCTION, signature, defLoc,
      isGlobal ? symbolTable.getGlobalScope() : symbolTable.getCurrentScope(), ctx);
  funcSymbol->isGlobal = isGlobal;

  bool defined;
  if (isGlobal) {
    funcSymbol->scope = symbolTable.getGlobalScope();
    defined = symbolTable.getGlobalScope()->define(funcSymbol);
    PLOGV << "尝试在全局作用域定义多返回值函数 '" << funcName << "'";
  } else {
    defined = symbolTable.defineSymbol(funcSymbol);
    PLOGV << "尝试在当前作用域定义多返回值函数 '" << funcName << "'";
  }

  if (!defined) { /* 处理重定义错误，与上面类似 */
    addDiagnostic(getRange(ctx->qualifiedIdentifier()), "多返回值函数 '" + funcName + "' 重复定义",
                  DiagnosticSeverity::Error);
  } else {
    PLOGD << "成功定义多返回值函数符号 '" << funcName << "'";
    if (auto qIdCtx = dynamic_cast<LangParser::QualifiedIdentifierContext *>(nameNode)) {
      if (!qIdCtx->IDENTIFIER().empty()) {
        analysisResult.nodeSymbolMap[qIdCtx->IDENTIFIER().back()] = funcSymbol; // 映射名字节点
      }
    }
  }

  // 3. 创建函数作用域
  ScopeGuard funcScopeGuard(symbolTable, ScopeKind::FUNCTION, ctx);
  recordScopeRange(symbolTable.getCurrentScope(), ctx);
  // 4. 压入预期返回类型 (用 nullptr 代表 mutivar)
  expectedReturnTypeStack.push(nullptr);
  PLOGV << "压入预期返回类型: mutivar (用 nullptr 表示)";

  // 5. 处理参数列表 (与上面类似)
  bool isVariadic = false;
  if (ctx->parameterList()) {
    std::any paramResult = visit(ctx->parameterList());
    try {
      auto paramsPair = std::any_cast<std::pair<std::vector<SymbolInfoPtr>, bool>>(paramResult);
      isVariadic = paramsPair.second;
      for (const auto &paramSymbol : paramsPair.first) {
        if (paramSymbol && paramSymbol->type) {
          signature->parameters.push_back({paramSymbol->name, paramSymbol->type});
        }
      }
    } catch (const std::bad_any_cast &e) {
      PLOGE << "内部错误: visitParameterList 返回类型不匹配 (mutivar). Error: "
            << e.what(); /* ... */
    } catch (const std::exception &e) {
      PLOGE << "访问 parameterList 结果时出错 (mutivar): " << e.what();
    }
  }
  signature->isVariadic = isVariadic;

  // 6. 访问函数体
  visit(ctx->blockStatement());

  // 7. 弹出预期返回类型
  if (!expectedReturnTypeStack.empty()) {
    PLOGV << "弹出预期返回类型 (mutivar)";
    expectedReturnTypeStack.pop();
  } else {
    PLOGE << "内部错误：尝试弹出空的预期返回类型栈 (mutivar)";
  }

  return std::any(funcSymbol);
}

/**
 * @brief 访问参数列表 (ParameterListContext)。
 * 处理 `param (, param)* [, ...]` 或 `...`
 * @return std::any 包含 std::pair<std::vector<SymbolInfoPtr>,
 * bool>，表示参数符号列表和是否为可变参数。
 */
std::any SemanticAnalyzerVisitor::visitParameterList(LangParser::ParameterListContext *ctx) {
  PLOGV << "visitParameterList";
  std::vector<SymbolInfoPtr> paramSymbols;
  bool isVariadic = false;

  // 遍历所有 parameter 子节点
  for (auto paramCtx : ctx->parameter()) {
    std::any result = visit(paramCtx); // 访问每个 parameter 节点
    try {
      SymbolInfoPtr ps = std::any_cast<SymbolInfoPtr>(result);
      if (ps) {
        paramSymbols.push_back(ps);
        PLOGV << " -> 添加参数符号: " << ps->name
              << " 类型: " << (ps->type ? ps->type->toString() : "<null>");
      } else {
        addDiagnostic(getRange(paramCtx), "无法处理参数定义", DiagnosticSeverity::Error);
      }
    } catch (const std::bad_any_cast &e) {
      PLOGE << "内部错误: visitParameter 返回类型不是 SymbolInfoPtr. Error: " << e.what();
      addDiagnostic(getRange(paramCtx), "内部错误：处理参数失败", DiagnosticSeverity::Error);
    } catch (const std::exception &e) {
      PLOGE << "访问 parameter 结果时出错: " << e.what();
    }
  }

  // 检查是否存在 '...' 可变参数标记
  // 语法是 parameter (COMMA parameter)* (COMMA DDD)? | DDD
  if (ctx->DDD()) {
    // 可能是只有 DDD，或者在参数列表最后有 COMMA DDD
    isVariadic = true;
    PLOGV << " -> 检测到可变参数 '...'";
  }

  // 返回包含参数符号列表和可变参数标志的 pair
  return std::any(std::make_pair(std::move(paramSymbols), isVariadic));
}

/**
 * @brief 访问单个参数 (ParameterContext)。
 * 处理 `type IDENTIFIER`
 * @return std::any 包含参数的 SymbolInfoPtr。
 */
std::any SemanticAnalyzerVisitor::visitParameter(LangParser::ParameterContext *ctx) {
  std::string paramName = ctx->IDENTIFIER()->getText();
  PLOGV << "visitParameter: " << paramName;

  Location defLoc = getLocation(ctx->IDENTIFIER()); // 参数名定义位置

  // 解析参数类型
  TypeInfoPtr paramType = resolveTypeFromContext(ctx->type());
  if (!paramType || paramType == UnknownType) {
    addDiagnostic(getRange(ctx->type()), "参数 '" + paramName + "' 的类型无效或未知",
                  DiagnosticSeverity::Error);
    paramType = UnknownType; // 标记为未知
  }
  // 检查是否使用 void 声明参数
  else if (paramType->isEqualTo(*VoidType)) {
    addDiagnostic(getRange(ctx->type()), "不能声明 void 类型的参数 '" + paramName + "'",
                  DiagnosticSeverity::Error);
    paramType = UnknownType; // 将类型重置为未知
  }

  // 创建参数符号
  // 参数的作用域是在调用 visitParameterList 时，由 visitFunctionDeclarationDef 创建的函数作用域
  SymbolInfoPtr paramSymbol = std::make_shared<SymbolInfo>(
      paramName, SymbolKind::PARAMETER, paramType, defLoc, symbolTable.getCurrentScope(), ctx);

  // 在当前（函数）作用域定义参数符号
  if (!symbolTable.defineSymbol(paramSymbol)) {
    SymbolInfoPtr existingSymbol = symbolTable.getCurrentScope()->resolveLocally(paramName);
    std::string errorMsg = "参数 '" + paramName + "' 在函数作用域中重复定义";
    if (existingSymbol) {
      errorMsg += "。先前定义在 line " +
                  std::to_string(existingSymbol->definitionLocation.range.start.line + 1);
    }
    addDiagnostic(getRange(ctx->IDENTIFIER()), errorMsg, DiagnosticSeverity::Error);
  } else {
    PLOGV << "成功定义参数符号 '" << paramName << "' 类型: " << paramType->toString();
  }

  return std::any(paramSymbol); // 返回参数符号信息
}

// --- 类声明 ---

/**
 * @brief 访问类声明定义 (ClassDeclarationDefContext)。
 * 处理 `class IDENTIFIER { classMember* }`
 * @return std::any 包含类符号的 SymbolInfoPtr。
 */
std::any
SemanticAnalyzerVisitor::visitClassDeclarationDef(LangParser::ClassDeclarationDefContext *ctx) {
  if (!ctx->IDENTIFIER()) {
    addDiagnostic(getRange(ctx), "无效的类定义", DiagnosticSeverity::Error);
    return std::any();
  }
  std::string className = ctx->IDENTIFIER()->getText();
  PLOGD << "visitClassDeclarationDef: " << className;
  Location defLoc = getLocation(ctx);

  // 1. 创建类类型信息 (ClassType)
  // 类作用域将在后面创建并关联
  auto classType = std::make_shared<ClassType>(className, nullptr, ctx); // 初始 scope 为 null

  // 2. 创建并定义类的符号 (类型为 ClassType)
  SymbolInfoPtr classSymbol = std::make_shared<SymbolInfo>(
      className, SymbolKind::CLASS, classType, defLoc, symbolTable.getCurrentScope(), ctx);

  // 类定义总是在当前作用域（通常是模块或全局）
  if (!symbolTable.defineSymbol(classSymbol)) {
    SymbolInfoPtr existingSymbol = symbolTable.getCurrentScope()->resolveLocally(className);
    std::string errorMsg = "类 '" + className + "' 重复定义";
    if (existingSymbol) {
      errorMsg += "。先前定义在 line " +
                  std::to_string(existingSymbol->definitionLocation.range.start.line + 1);
    }
    addDiagnostic(getRange(ctx->IDENTIFIER()), errorMsg, DiagnosticSeverity::Error);
    // 即使重定义，也继续分析类内部
  } else {
    PLOGD << "成功定义类符号 '" << className << "'";
  }

  // 3. 创建类的作用域
  // 类作用域是当前作用域的子作用域
  // 注意：这里我们先推入作用域，然后才将作用域关联到 ClassType 对象
  symbolTable.pushScope(ScopeKind::CLASS, ctx);
  std::shared_ptr<Scope> classScope = symbolTable.getCurrentScope();
  classType->classScope = classScope; // 将新创建的作用域关联到 ClassType

  // RAII Guard 来确保作用域正确弹出，即使在异常情况下
  // 这里手动管理 ScopeGuard，因为我们需要在 visitChildren 之前设置 currentClassType
  ScopeGuard classScopeGuard(symbolTable, ScopeKind::CLASS,
                             ctx); // 创建一个空的 Guard，因为它不会做 push 操作
  recordScopeRange(classScope, ctx);
  classScopeGuard.pop(); // 立刻标记为 popped，防止析构时再次 pop，我们会在下面手动管理

  // 4. 设置当前类上下文，用于 this 和成员访问
  ClassType *previousClassType = currentClassType; // 保存外部的类上下文
  currentClassType = classType.get();              // 设置当前正在分析的类

  // 5. 访问所有类成员 (字段和方法)
  for (auto memberCtx : ctx->classMember()) {
    visit(memberCtx); // 访问每个成员
  }

  // 6. 恢复外部类上下文
  currentClassType = previousClassType;

  // 7. 显式弹出类作用域
  PLOGD << "手动弹出类作用域 for " << className;
  symbolTable.popScope(); // 手动弹出

  return std::any(classSymbol); // 返回类符号信息
}

/**
 * @brief 访问类字段成员 (ClassFieldMemberContext)。
 * 处理 `[static?] [const?] declaration_item [ = expression ]`
 * @return std::any 包含字段符号的 SymbolInfoPtr (或空)。
 */
std::any SemanticAnalyzerVisitor::visitClassFieldMember(LangParser::ClassFieldMemberContext *ctx) {
  bool isStatic = (ctx->STATIC() != nullptr);
  bool isConst = (ctx->CONST() != nullptr);
  PLOGV << "visitClassFieldMember" << (isStatic ? " (Static)" : "") << (isConst ? " (Const)" : "");

  // 字段声明必须在类作用域内
  if (!currentClassType || symbolTable.getCurrentScope()->kind != ScopeKind::CLASS) {
    addDiagnostic(getRange(ctx), "字段声明只能出现在类定义内部", DiagnosticSeverity::Error);
    return std::any();
  }

  // 1. 获取字段名
  std::string fieldName = ctx->declaration_item()->IDENTIFIER()->getText();
  Location defLoc = getLocation(ctx->declaration_item()->IDENTIFIER());

  // 2. 确定字段类型
  TypeInfoPtr fieldType = UnknownType;
  if (ctx->declaration_item()->type()) {
    fieldType = resolveTypeFromContext(ctx->declaration_item()->type());
    if (!fieldType || fieldType == UnknownType) { /* 错误处理 */
      fieldType = UnknownType;
    }
    if (fieldType->isEqualTo(*VoidType)) {
      addDiagnostic(getRange(ctx->declaration_item()->type()),
                    "不能声明 void 类型的字段 '" + fieldName + "'", DiagnosticSeverity::Error);
      fieldType = UnknownType;
    }
  } else if (ctx->declaration_item()->AUTO()) {
    if (ctx->expression()) {
      fieldType = inferExpressionType(ctx->expression());
      if (!fieldType || fieldType == UnknownType) { /* 警告/错误 */
        fieldType = AnyType;
      } else if (fieldType->isEqualTo(*VoidType)) {
        addDiagnostic(getRange(ctx->expression()),
                      "不能用返回 void 的表达式初始化 'auto' 字段 '" + fieldName + "'",
                      DiagnosticSeverity::Error);
        fieldType = UnknownType;
      }
    } else {
      addDiagnostic(getRange(ctx->declaration_item()->AUTO()),
                    "'auto' 字段 '" + fieldName + "' 必须被初始化", DiagnosticSeverity::Error);
      fieldType = UnknownType;
    }
  } else { /* 错误 */
    fieldType = UnknownType;
  }

  // 3. 创建字段符号
  SymbolInfoPtr fieldSymbol = std::make_shared<SymbolInfo>(
      fieldName, SymbolKind::VARIABLE, fieldType, defLoc, symbolTable.getCurrentScope(), ctx);
  fieldSymbol->isConst = isConst;
  fieldSymbol->isStatic = isStatic; // 标记是否为静态字段

  // 4. 在当前（类）作用域定义字段符号
  if (!symbolTable.defineSymbol(fieldSymbol)) {
    // 处理字段重定义错误
    SymbolInfoPtr existingSymbol = symbolTable.getCurrentScope()->resolveLocally(fieldName);
    std::string errorMsg = (isStatic ? "静态" : "实例");
    errorMsg += "字段 '" + fieldName + "' 在类 '" + currentClassType->name + "' 中重复定义";
    if (existingSymbol) {
      errorMsg += "。先前定义在 line " +
                  std::to_string(existingSymbol->definitionLocation.range.start.line + 1);
    }
    addDiagnostic(getRange(ctx->declaration_item()->IDENTIFIER()), errorMsg,
                  DiagnosticSeverity::Error);
    return std::any(); // 返回空 any 表示失败
  } else {
    PLOGD << "成功定义字段 '" << fieldName << "' 类型: " << fieldType->toString()
          << (isStatic ? " (Static)" : "");
  }

  // 5. 处理初始化表达式（如果存在）
  if (ctx->expression()) {
    TypeInfoPtr initExprType = inferExpressionType(ctx->expression());
    if (!initExprType || initExprType == UnknownType) { /* 警告 */
    } else {
      // 检查类型兼容性
      if (fieldType != UnknownType &&
          !checkTypeAssignable(initExprType, fieldType, ctx->expression())) {
        // checkTypeAssignable 已添加诊断
      }
      // 更新 auto 类型
      if (ctx->declaration_item()->AUTO() && fieldType != UnknownType &&
          initExprType != UnknownType && fieldType != initExprType) {
        fieldType = initExprType;
        fieldSymbol->type = fieldType;
      }
    }
    // TODO: 静态字段的初始化表达式可能需要限制（例如，只能是常量表达式）
    if (isStatic) {
      // 可以在这里检查初始化表达式是否为常量（如果语言要求）
      // addDiagnostic(getRange(ctx->expression()), "静态字段的初始化表达式必须是常量",
      // DiagnosticSeverity::Error);
    }
  } else if (isConst) {
    addDiagnostic(getRange(ctx->declaration_item()->IDENTIFIER()),
                  "'const' 字段 '" + fieldName + "' 必须被初始化", DiagnosticSeverity::Error);
  }

  return std::any(fieldSymbol); // 返回字段符号
}

/**
 * @brief 访问类方法成员 (ClassMethodMemberContext)。
 * 处理 `[static?] type IDENTIFIER ( [paramList] ) blockStatement`
 * @return std::any 包含方法符号的 SymbolInfoPtr。
 */
std::any
SemanticAnalyzerVisitor::visitClassMethodMember(LangParser::ClassMethodMemberContext *ctx) {
  bool isStatic = (ctx->STATIC() != nullptr);
  antlr4::tree::TerminalNode *identifierNode = ctx->IDENTIFIER(); // 获取名字节点
  std::string methodName = identifierNode->getText();
  PLOGD << "visitClassMethodMember: " << methodName << (isStatic ? " (Static)" : "");

  // 方法声明必须在类作用域内
  if (!currentClassType || symbolTable.getCurrentScope()->kind != ScopeKind::CLASS) {
    addDiagnostic(getRange(ctx), "方法声明只能出现在类定义内部", DiagnosticSeverity::Error);
    return std::any();
  }

  Location defLoc = getLocation(ctx->IDENTIFIER());

  // 1. 解析返回类型
  TypeInfoPtr returnType = resolveTypeFromContext(ctx->type());
  if (!returnType || returnType == UnknownType) {
    addDiagnostic(getRange(ctx->type()), "方法 '" + methodName + "' 的返回类型无效或未知",
                  DiagnosticSeverity::Warning);
    returnType = UnknownType;
  }

  // 2. 创建方法签名和符号
  auto signature = std::make_shared<FunctionSignature>();
  signature->returnTypeInfo = returnType;
  // 注意：方法也是 FUNCTION kind，通过 isStatic 和其作用域（ScopeKind::CLASS）来区分
  SymbolInfoPtr methodSymbol = std::make_shared<SymbolInfo>(
      methodName, SymbolKind::FUNCTION, signature, defLoc, symbolTable.getCurrentScope(), ctx);
  methodSymbol->isStatic = isStatic;

  // 3. 在当前（类）作用域定义方法符号
  if (!symbolTable.defineSymbol(methodSymbol)) {
    // 处理方法重定义错误
    SymbolInfoPtr existingSymbol = symbolTable.getCurrentScope()->resolveLocally(methodName);
    std::string errorMsg = (isStatic ? "静态" : "实例");
    errorMsg += "方法 '" + methodName + "' 在类 '" + currentClassType->name + "' 中重复定义";
    if (existingSymbol) {
      errorMsg += "。先前定义在 line " +
                  std::to_string(existingSymbol->definitionLocation.range.start.line + 1);
    }
    addDiagnostic(getRange(ctx->IDENTIFIER()), errorMsg, DiagnosticSeverity::Error);
    // 即使重定义，仍然分析方法体
  } else {
    PLOGD << "成功定义方法符号 '" << methodName << "'" << (isStatic ? " (Static)" : "");
    analysisResult.nodeSymbolMap[identifierNode] = methodSymbol; // 映射方法名节点
  }
  recordScopeRange(symbolTable.getCurrentScope(), ctx);
  // 4. 创建方法作用域
  ScopeGuard methodScopeGuard(symbolTable, ScopeKind::FUNCTION, ctx);
  recordScopeRange(symbolTable.getCurrentScope(), ctx);
  recordScopeRange(symbolTable.getCurrentScope(), ctx);
  // 5. 处理 'this' (仅对非静态方法) 和 设置上下文
  bool previousInStatic = isInStaticMethod; // 保存外部状态
  isInStaticMethod = isStatic;              // 设置当前状态

  // 6. 将预期返回类型压栈
  expectedReturnTypeStack.push(returnType);
  PLOGV << "压入预期返回类型: " << (returnType ? returnType->toString() : "null");

  // 7. 访问参数列表，在方法作用域内定义参数符号
  bool isVariadic = false;
  if (ctx->parameterList()) {
    std::any paramResult = visit(ctx->parameterList());
    try {
      auto paramsPair = std::any_cast<std::pair<std::vector<SymbolInfoPtr>, bool>>(paramResult);
      isVariadic = paramsPair.second;
      for (const auto &paramSymbol : paramsPair.first) {
        if (paramSymbol && paramSymbol->type) {
          signature->parameters.push_back({paramSymbol->name, paramSymbol->type});
        }
      }
    } catch (...) { /* 错误处理 */
    }
  }
  signature->isVariadic = isVariadic;

  // 8. 访问方法体
  visit(ctx->blockStatement());

  // 9. 弹出预期返回类型
  if (!expectedReturnTypeStack.empty()) {
    PLOGV << "弹出预期返回类型";
    expectedReturnTypeStack.pop();
  } else {
    PLOGE << "内部错误：尝试弹出空的预期返回类型栈 (方法)";
  }

  // 10. 恢复静态方法上下文
  isInStaticMethod = previousInStatic;

  // ScopeGuard 会自动弹出方法作用域

  return std::any(methodSymbol); // 返回方法符号
}

/**
 * @brief 访问类中的空成员 (ClassEmptyMemberContext)。
 * 对应于类定义中可能出现的单独分号 ';'. 通常无语义动作。
 */
std::any SemanticAnalyzerVisitor::visitClassEmptyMember(LangParser::ClassEmptyMemberContext *ctx) {
  // ctx 参数未使用
  (void)ctx;
  PLOGV << "visitClassEmptyMember";
  // 空成员，通常不需要做任何事情
  return std::any();
}

// --- 结束 Part 2 ---
// src/semantic_analyzer_visitor.cpp

// --- Part 1 and Part 2 code should be here ---
// ...

// ============================================================================
// Part 3: 基础表达式与后缀表达式 (完整实现)
// ============================================================================

// --- 表达式处理的核心（部分实现） ---

// --- Primary Expression Visits ---

/**
 * @brief 访问 Primary 表达式中的原子字面量。
 */
std::any SemanticAnalyzerVisitor::visitPrimaryAtom(LangParser::PrimaryAtomContext *ctx) {
  PLOGV << "visitPrimaryAtom: " << ctx->getText();
  return visit(ctx->atomexp());
}

/**
 * @brief 访问 Primary 表达式中的标识符。
 */
std::any
SemanticAnalyzerVisitor::visitPrimaryIdentifier(LangParser::PrimaryIdentifierContext *ctx) {
  antlr4::tree::TerminalNode *identifierNode = ctx->IDENTIFIER();
  std::string name = identifierNode->getText();
  PLOGV << "visitPrimaryIdentifier: " << name;
  SymbolInfoPtr symbol = symbolTable.resolveSymbol(name);
  if (!symbol) {
    addDiagnostic(getRange(ctx->IDENTIFIER()), "未声明的标识符 '" + name + "'",
                  DiagnosticSeverity::Error);
    return std::any(UnknownType);
  }
  PLOGV << " -> 找到符号 '" << name
        << "'，类型: " << (symbol->type ? symbol->type->toString() : "<null>");
  analysisResult.nodeSymbolMap[identifierNode] = symbol; // 映射使用点到定义
  // TODO: 添加对符号使用的检查 (例如，变量使用前是否初始化)
  // 可以将符号信息附加到 ctx (如果 ANTLR 允许扩展) 或使用外部 map
  // ctx->resolvedSymbol = symbol;
  return std::any(symbol->type ? symbol->type : UnknownType);
}

/**
 * @brief 访问括号表达式。
 */
std::any SemanticAnalyzerVisitor::visitPrimaryParenExp(LangParser::PrimaryParenExpContext *ctx) {
  PLOGV << "visitPrimaryParenExp: " << ctx->getText();
  // 括号表达式的类型就是内部表达式的类型
  return std::any(inferExpressionType(ctx->expression()));
}

// --- Postfix Expression Visits ---

/**
 * @brief 访问后缀表达式。
 */
std::any
SemanticAnalyzerVisitor::visitPostfixExpression(LangParser::PostfixExpressionContext *ctx) {
  PLOGV << "visitPostfixExpression: " << ctx->getText();
  std::any baseResult = visit(ctx->primaryExp());
  TypeInfoPtr currentType = UnknownType;
  try {
    currentType = std::any_cast<TypeInfoPtr>(baseResult);
    if (!currentType)
      currentType = UnknownType;
  } catch (const std::bad_any_cast &) {
    PLOGV << "后缀表达式的基础部分未返回有效类型。";
    currentType = UnknownType;
  } catch (const std::exception &e) {
    PLOGE << "获取后缀表达式基础类型时出错: " << e.what();
    currentType = UnknownType;
  }

  for (auto suffixCtxVariant : ctx->postfixSuffix()) {
    PLOGV << "  处理后缀: " << suffixCtxVariant->getText();
    if (currentType == UnknownType || !currentType) {
      PLOGV << "  -> 因当前类型未知，跳过后缀处理。";
      break;
    }
    std::any suffixResult = visitSuffix(suffixCtxVariant, currentType); // 调用辅助函数
    try {
      currentType = std::any_cast<TypeInfoPtr>(suffixResult);
      if (!currentType)
        currentType = UnknownType;
      PLOGV << "  -> 后缀处理后类型变为: " << currentType->toString();
    } catch (const std::bad_any_cast &) {
      PLOGV << "  -> 后缀处理未返回有效类型。";
      currentType = UnknownType;
    } catch (const std::exception &e) {
      PLOGE << "处理后缀时出错: " << e.what();
      currentType = UnknownType;
    }
  }
  PLOGV << "visitPostfixExpression 结束, 最终类型: "
        << (currentType ? currentType->toString() : "<null>");
  return std::any(currentType);
}

/**
 * @brief 辅助函数，用于分发后缀访问。(需要头文件声明)
 */
std::any SemanticAnalyzerVisitor::visitSuffix(LangParser::PostfixSuffixContext *suffixNode,
                                              const TypeInfoPtr &baseType) {
  if (auto indexCtx = dynamic_cast<LangParser::PostfixIndexSuffixContext *>(suffixNode)) {
    return visitPostfixIndexSuffix(indexCtx, baseType);
  } else if (auto memberCtx = dynamic_cast<LangParser::PostfixMemberSuffixContext *>(suffixNode)) {
    return visitPostfixMemberSuffix(memberCtx, baseType);
  } else if (auto callCtx = dynamic_cast<LangParser::PostfixCallSuffixContext *>(suffixNode)) {
    return visitPostfixCallSuffix(callCtx, baseType);
  } else if (auto colonCtx =
                 dynamic_cast<LangParser::PostfixColonLookupSuffixContext *>(suffixNode)) {
    addDiagnostic(getRange(colonCtx), "成员查找操作符 ':' 暂未完全支持语义分析",
                  DiagnosticSeverity::Warning);
    return visitPostfixMemberSuffix_like(colonCtx->IDENTIFIER(), baseType, colonCtx);
  }
  PLOGW << "未知的后缀类型";
  return std::any(UnknownType);
}

/**
 * @brief 访问索引后缀。(需要头文件声明)
 */
std::any
SemanticAnalyzerVisitor::visitPostfixIndexSuffix(LangParser::PostfixIndexSuffixContext *ctx,
                                                 const TypeInfoPtr &baseType) {
  PLOGV << "visitPostfixIndexSuffix";
  TypeInfoPtr indexExprType = inferExpressionType(ctx->expression());
  TypeInfoPtr resultType = UnknownType;
  if (!baseType)
    return std::any(UnknownType); // 防御性编程

  if (baseType->isEqualTo(*AnyType) || indexExprType->isEqualTo(*AnyType)) {
    PLOGV << " -> 索引访问涉及 'any'，结果类型为 'any'";
    return std::any(AnyType);
  }
  if (auto listType = std::dynamic_pointer_cast<ListType>(baseType)) {
    if (checkTypeAssignable(indexExprType, IntType, ctx->expression()) ||
        checkTypeAssignable(indexExprType, NumberType, ctx->expression())) {
      resultType = listType->elementType;
      PLOGV << " -> 索引 list<" << resultType->toString() << ">，返回元素类型";
    } else {
      addDiagnostic(getRange(ctx->expression()),
                    "列表索引必须是整数类型，实际为 '" + indexExprType->toString() + "'",
                    DiagnosticSeverity::Error);
      resultType = UnknownType;
    }
  } else if (auto mapType = std::dynamic_pointer_cast<MapType>(baseType)) {
    if (checkTypeAssignable(indexExprType, mapType->keyType, ctx->expression())) {
      resultType = mapType->valueType;
      PLOGV << " -> 索引 map<" << mapType->keyType->toString() << ","
            << mapType->valueType->toString() << ">，返回值的类型";
    } else {
      addDiagnostic(getRange(ctx->expression()),
                    "映射索引类型 '" + indexExprType->toString() + "' 与键类型 '" +
                        mapType->keyType->toString() + "' 不兼容",
                    DiagnosticSeverity::Error);
      resultType = UnknownType;
    }
  } else if (auto stringType = std::dynamic_pointer_cast<BaseType>(baseType);
             stringType && stringType->kind == BaseTypeKind::STRING) {
    if (checkTypeAssignable(indexExprType, IntType, ctx->expression()) ||
        checkTypeAssignable(indexExprType, NumberType, ctx->expression())) {
      resultType = StringType;
      PLOGV << " -> 索引 string，返回 string 类型";
    } else {
      addDiagnostic(getRange(ctx->expression()),
                    "字符串索引必须是整数类型，实际为 '" + indexExprType->toString() + "'",
                    DiagnosticSeverity::Error);
      resultType = UnknownType;
    }
  } else {
    addDiagnostic(getRange(ctx), "类型 '" + baseType->toString() + "' 不支持索引操作 '[]'",
                  DiagnosticSeverity::Error);
    resultType = UnknownType;
  }
  return std::any(resultType);
}

struct a {
  char c;
  long long b;
};

/**
 * @brief 访问成员访问后缀。(需要头文件声明)
 */
std::any
SemanticAnalyzerVisitor::visitPostfixMemberSuffix(LangParser::PostfixMemberSuffixContext *ctx,
                                                  const TypeInfoPtr &baseType) {
  return visitPostfixMemberSuffix_like(ctx->IDENTIFIER(), baseType, ctx);
}

/**
 * @brief 处理成员访问的通用逻辑。(需要头文件声明)
 */
std::any SemanticAnalyzerVisitor::visitPostfixMemberSuffix_like(
    antlr4::tree::TerminalNode *identifierNode, const TypeInfoPtr &baseType,
    antlr4::ParserRuleContext *errorNode) // errorNode 用于报告错误位置
{
  // 增加 nullptr 检查
  if (!identifierNode || !baseType || !errorNode) {
    PLOGW << "visitPostfixMemberSuffix_like 收到无效参数，返回 UnknownType";
    return std::any(UnknownType);
  }

  std::string memberName = identifierNode->getText();
  PLOGV << "visitPostfixMemberSuffix_like: 成员 '" << memberName << "' on base type "
        << baseType->toString();
  TypeInfoPtr resultType = UnknownType;

  if (baseType->isEqualTo(*AnyType)) {
    PLOGV << " -> 基础类型是 'any'，成员访问结果类型为 'any'";
    resultType = AnyType;
  } else if (auto classType = std::dynamic_pointer_cast<ClassType>(baseType)) {
    if (!classType->classScope) {
      addDiagnostic(getRange(errorNode), "内部错误：类 '" + classType->name + "' 没有关联的作用域",
                    DiagnosticSeverity::Error);
    } else {
      SymbolInfoPtr memberSymbol = classType->classScope->resolveLocally(memberName); // 本地查找
      if (!memberSymbol) {
        addDiagnostic(getRange(identifierNode),
                      "类 '" + classType->name + "' 中没有名为 '" + memberName + "' 的成员",
                      DiagnosticSeverity::Error);
      } else {
        // --- **关键: 填充成员标识符节点的映射** ---
        analysisResult.nodeSymbolMap[identifierNode] = memberSymbol;
        // --- 结束填充 ---

        // 检查访问上下文（是实例访问还是类访问？） - 这里假设是实例访问
        // （如果支持类名访问静态成员，需要额外逻辑区分）
        if (memberSymbol->isStatic && dynamic_cast<LangParser::PostfixMemberSuffixContext *>(
                                          errorNode)) { // 检查是否是 '.' 访问
          addDiagnostic(getRange(identifierNode),
                        "不能通过实例 '.' 访问静态成员 '" + memberName + "'，请使用 ':' 或类名",
                        DiagnosticSeverity::Warning); // 或 Error?
        }
        if (!memberSymbol->isStatic && dynamic_cast<LangParser::PostfixColonLookupSuffixContext *>(
                                           errorNode)) { // 检查是否是 ':' 访问
          addDiagnostic(getRange(identifierNode),
                        "不能通过 ':' 访问非静态成员 '" + memberName + "'",
                        DiagnosticSeverity::Warning); // 或 Error?
        }

        resultType = memberSymbol->type ? memberSymbol->type : UnknownType;
        PLOGV << " -> 在类 '" << classType->name << "' 中找到成员 '" << memberName
              << "'，类型: " << (resultType ? resultType->toString() : "<null>");
      }
    }
  }
  // TODO: 处理其他可能支持成员访问的基础类型或模块类型 (例如 namespace import)
  else {
    addDiagnostic(getRange(errorNode),
                  "类型 '" + baseType->toString() + "' 不支持成员访问操作符 '.' 或 ':'",
                  DiagnosticSeverity::Error);
  }

  return std::any(resultType);
}

/**
 * @brief 访问函数调用后缀。(需要头文件声明)
 */
std::any SemanticAnalyzerVisitor::visitPostfixCallSuffix(LangParser::PostfixCallSuffixContext *ctx,
                                                         const TypeInfoPtr &baseType) {
  PLOGV << "visitPostfixCallSuffix";
  if (!baseType)
    return std::any(UnknownType); // 防御

  if (baseType->isEqualTo(*AnyType)) {
    PLOGV << " -> 调用 'any' 类型，结果类型为 'any'";
    if (ctx->arguments()) {
      visit(ctx->arguments());
    }
    return std::any(AnyType);
  }
  if (auto funcSig = std::dynamic_pointer_cast<FunctionSignature>(baseType)) {
    PLOGV << " -> 正在调用函数签名: " << funcSig->toString();
    std::vector<TypeInfoPtr> argTypes;
    if (ctx->arguments()) { // 检查 arguments 是否存在
      std::any argListResult =
          visit(ctx->arguments()); // visitArguments 现在返回 vector<TypeInfoPtr>
      try {
        argTypes = std::any_cast<std::vector<TypeInfoPtr>>(argListResult);
      } catch (const std::bad_any_cast &) {
        addDiagnostic(getRange(ctx->arguments()), "内部错误：参数列表未返回预期类型",
                      DiagnosticSeverity::Error);
      } catch (const std::exception &e) {
        PLOGE << "访问参数列表结果时出错: " << e.what();
      }
    } // else: 没有参数，argTypes 为空
    PLOGV << " -> 实际参数数量: " << argTypes.size();

    size_t requiredParams = funcSig->parameters.size();
    size_t providedArgs = argTypes.size();

    // 参数数量检查
    bool argCountOk = true;
    if (funcSig->isVariadic) {
      if (providedArgs < requiredParams) {
        argCountOk = false;
        addDiagnostic(getRange(ctx->CP()),
                      "函数调用缺少参数，期望至少 " + std::to_string(requiredParams) +
                          " 个，实际提供 " + std::to_string(providedArgs),
                      DiagnosticSeverity::Error);
      }
    } else {
      if (providedArgs != requiredParams) {
        argCountOk = false;
        addDiagnostic(getRange(ctx->CP()),
                      "函数调用参数数量不匹配，期望 " + std::to_string(requiredParams) +
                          " 个，实际提供 " + std::to_string(providedArgs),
                      DiagnosticSeverity::Error);
      }
    }
    if (!argCountOk)
      return std::any(UnknownType); // 数量不对，无法确定返回类型

    PLOGV << " -> 参数数量检查通过";

    // 参数类型检查
    for (size_t i = 0; i < requiredParams; ++i) {
      if (argTypes[i] != UnknownType && argTypes[i] != AnyType) { // 仅在实际类型已知且非 any 时检查
        TypeInfoPtr expectedType = funcSig->parameters[i].second;
        TypeInfoPtr actualType = argTypes[i];
        // 获取参数对应的表达式节点用于报告错误位置
        antlr4::ParserRuleContext *argExprCtx = nullptr;
        if (ctx->arguments() && ctx->arguments()->expressionList() &&
            i < ctx->arguments()->expressionList()->expression().size()) {
          argExprCtx = ctx->arguments()->expressionList()->expression(i);
        }
        if (!checkTypeAssignable(actualType, expectedType, argExprCtx)) {
          PLOGW << " -> 参数类型不匹配 (索引 " << i << "): 期望 '" << expectedType->toString()
                << "', 得到 '" << actualType->toString() << "'";
        } else {
          PLOGV << " -> 参数类型匹配 (索引 " << i << "): '" << actualType->toString() << "' -> '"
                << expectedType->toString() << "'";
        }
      } else {
        PLOGV << " -> 跳过参数类型检查 (索引 " << i << ")，因为实际类型是 Unknown 或 Any";
      }
    }

    // 返回类型
    if (std::holds_alternative<TypeInfoPtr>(funcSig->returnTypeInfo)) {
      TypeInfoPtr retType = std::get<TypeInfoPtr>(funcSig->returnTypeInfo);
      PLOGV << " -> 函数返回类型: " << (retType ? retType->toString() : "void");
      return std::any(retType ? retType : VoidType);
    } else if (std::holds_alternative<FunctionSignature::MultiReturnTag>(funcSig->returnTypeInfo)) {
      PLOGV << " -> 函数是多返回值 (mutivar)，调用结果类型设为 Unknown";
      return std::any(UnknownType);
    } else {
      PLOGE << "内部错误：未知的 FunctionSignature returnTypeInfo variant";
      return std::any(UnknownType);
    }
  } else {
    addDiagnostic(getRange(ctx), "尝试调用非函数类型 '" + baseType->toString() + "'",
                  DiagnosticSeverity::Error);
    return std::any(UnknownType);
  }
}

// --- Unary Operation ---

/**
 * @brief 访问从一元到后缀的传递规则。
 */
std::any SemanticAnalyzerVisitor::visitUnaryToPostfix(LangParser::UnaryToPostfixContext *ctx) {
  PLOGV << "visitUnaryToPostfix";
  return visit(ctx->postfixExp());
}

// --- 赋值语句的访问 ---

/**
 * @brief 访问赋值语句 (AssignStmtContext)。
 */
std::any SemanticAnalyzerVisitor::visitAssignStmt(LangParser::AssignStmtContext *ctx) {
  PLOGV << "visitAssignStmt";
  return visit(ctx->assignStatement());
}

/**
 * @brief 访问更新赋值语句 (UpdateStmtContext)。
 */
std::any SemanticAnalyzerVisitor::visitUpdateStmt(LangParser::UpdateStmtContext *ctx) {
  PLOGV << "visitUpdateStmt";
  return visit(ctx->updateStatement());
}

// ============================================================================
// Part 3: 基础表达式与后缀表达式
// ============================================================================

// --- 表达式处理的核心 ---
TypeInfoPtr SemanticAnalyzerVisitor::inferExpressionType(LangParser::ExpressionContext *ctx) {
  if (!ctx) {
    PLOGW << "inferExpressionType 收到空的 ExpressionContext*";
    return UnknownType;
  }
  PLOGV << "推断表达式类型: " << ctx->getText();
  if (ctx->logicalOrExp()) {
    std::any result = visit(ctx->logicalOrExp());
    try {
      auto inferredType = std::any_cast<TypeInfoPtr>(result);
      if (inferredType) {
        PLOGV << " -> 表达式最终推断得到类型: " << inferredType->toString();
        return inferredType;
      }
      PLOGW << " -> 推断失败，子规则 logicalOrExp 未返回有效类型。";
      return UnknownType;
    } catch (const std::bad_any_cast &) {
      PLOGV << " -> 子规则 logicalOrExp 未返回 TypeInfoPtr";
    } catch (const std::exception &e) {
      addDiagnostic(getRange(ctx), std::string("内部错误：推断表达式类型时出错: ") + e.what(),
                    DiagnosticSeverity::Error);
      return UnknownType;
    } catch (...) {
      addDiagnostic(getRange(ctx), "内部错误：推断表达式类型时发生未知错误",
                    DiagnosticSeverity::Error);
      return UnknownType;
    }
  } else {
    PLOGE << "内部错误：ExpressionContext 没有 logicalOrExp 子节点。";
    addDiagnostic(getRange(ctx), "内部错误：无法处理的表达式结构", DiagnosticSeverity::Error);
  }
  addDiagnostic(getRange(ctx), "无法推断表达式类型: " + ctx->getText(),
                DiagnosticSeverity::Warning);
  return UnknownType;
}

// --- Unary Operation ---
std::any SemanticAnalyzerVisitor::visitUnaryPrefix(LangParser::UnaryPrefixContext *ctx) { /* ... */
  PLOGV << "visitUnaryPrefix: " << ctx->getText();
  TypeInfoPtr operandType = UnknownType;
  std::any operandResult = visit(ctx->unaryExp());
  try {
    operandType = std::any_cast<TypeInfoPtr>(operandResult);
    if (!operandType)
      operandType = UnknownType;
  } catch (...) {
    operandType = UnknownType;
  }
  if (operandType == UnknownType) {
    PLOGV << " -> 操作数类型未知，无法确定一元操作结果类型";
    return std::any(UnknownType);
  }
  if (ctx->NOT()) {
    PLOGV << " -> 逻辑非 '!'，结果类型: bool";
    return std::any(BoolType);
  } else if (ctx->SUB()) {
    if (operandType->isEqualTo(*IntType) || operandType->isEqualTo(*FloatType) ||
        operandType->isEqualTo(*NumberType) || operandType->isEqualTo(*AnyType)) {
      PLOGV << " -> 负号 '-'，结果类型: " << operandType->toString();
      return std::any(operandType);
    } else {
      addDiagnostic(getRange(ctx->SUB()),
                    "负号 '-' 操作符只能用于数字类型，不能用于 '" + operandType->toString() + "'",
                    DiagnosticSeverity::Error);
      return std::any(UnknownType);
    }
  } else if (ctx->LEN()) {
    if (operandType->isEqualTo(*StringType) || std::dynamic_pointer_cast<ListType>(operandType) ||
        std::dynamic_pointer_cast<MapType>(operandType) || operandType->isEqualTo(*AnyType)) {
      PLOGV << " -> 长度 '#'，结果类型: int";
      return std::any(IntType);
    } else {
      addDiagnostic(getRange(ctx->LEN()),
                    "长度操作符 '#' 不能用于类型 '" + operandType->toString() + "'",
                    DiagnosticSeverity::Error);
      return std::any(UnknownType);
    }
  } else if (ctx->BIT_NOT()) {
    if (operandType->isEqualTo(*IntType) || operandType->isEqualTo(*AnyType)) {
      PLOGV << " -> 按位取反 '~'，结果类型: int";
      return std::any(IntType);
    } else {
      addDiagnostic(getRange(ctx->BIT_NOT()),
                    "按位取反 '~' 操作符只能用于整数类型，不能用于 '" + operandType->toString() +
                        "'",
                    DiagnosticSeverity::Error);
      return std::any(UnknownType);
    }
  }
  PLOGE << "内部错误：未知的一元前缀操作符";
  return std::any(UnknownType);
}

// --- 赋值语句的访问 ---
std::any SemanticAnalyzerVisitor::visitNormalAssignStmt(LangParser::NormalAssignStmtContext *ctx) {
  PLOGV << "visitNormalAssignStmt (Multi-Assignment): " << ctx->getText();

  // 获取左值和右值列表
  auto lvalues = ctx->lvalue();         // 现在返回 std::vector<LvalueContext*>
  auto expressions = ctx->expression(); // 现在返回 std::vector<ExpressionContext*>
  size_t numLvalues = lvalues.size();
  size_t numExpressions = expressions.size();

  PLOGV << " -> Lvalues: " << numLvalues << ", Expressions: " << numExpressions;

  // --- 1. 处理匹配的部分 ---
  size_t commonCount = std::min(numLvalues, numExpressions);
  for (size_t i = 0; i < commonCount; ++i) {
    LangParser::LvalueContext *lval_ctx = lvalues[i];         // 获取第 i 个左值上下文
    LangParser::ExpressionContext *expr_ctx = expressions[i]; // 获取第 i 个右值上下文

    if (!lval_ctx || !expr_ctx) {
      PLOGE << "  -> Error: Null context at index " << i;
      continue;
    }

    // 初始化此迭代的变量
    TypeInfoPtr lvalueType = nullptr;
    bool lvalueIsConst = false;
    SymbolInfoPtr lvalueBaseSymbol = nullptr; // 用于检查 const

    // 访问左值以推断其类型
    std::any lvalueResult = visit(lval_ctx); // 访问单个 lvalue 子树
    try {
      lvalueType = std::any_cast<TypeInfoPtr>(lvalueResult);
      if (!lvalueType)
        lvalueType = UnknownType; // 处理 visit 返回空类型

      // --- 检查左值是否为 const ---
      // 注意: 这个检查只适用于简单的 lvalue (如标识符或 this)
      // 对于复杂的 lvalue (a.b, list[i]) 需要更复杂的分析来确定其最终是否可写
      if (auto lvalueBaseCtx = dynamic_cast<LangParser::LvalueBaseContext *>(lval_ctx)) {
        if (lvalueBaseCtx->IDENTIFIER()) {
          // 查找基础标识符的符号
          lvalueBaseSymbol = symbolTable.resolveSymbol(lvalueBaseCtx->IDENTIFIER()->getText());
          if (lvalueBaseSymbol && lvalueBaseSymbol->isConst) {
            lvalueIsConst = true;
            PLOGV << "  -> Lvalue (index " << i << ") '" << lval_ctx->getText()
                  << "' is const (base symbol).";
          }
        }
        // TODO: 添加对 lvalueSuffix 的检查，例如 obj.const_field
      } else {
        // 如果不是 LvalueBaseContext，我们可能无法简单地确定它是否 const
        PLOGV << "  -> Lvalue (index " << i << ") is complex, const check simplified.";
      }
      // --- 结束 const 检查 ---

    } catch (const std::bad_any_cast &) {
      addDiagnostic(getRange(lval_ctx), "无效的赋值左侧", DiagnosticSeverity::Error);
      continue;
    } catch (const std::exception &e) {
      addDiagnostic(getRange(lval_ctx), std::string("处理赋值左侧时出错: ") + e.what(),
                    DiagnosticSeverity::Error);
      continue;
    }

    // 如果左值是常量，报告错误
    if (lvalueIsConst) {
      addDiagnostic(getRange(lval_ctx), "不能给常量 '" + lval_ctx->getText() + "' 赋值",
                    DiagnosticSeverity::Error);
      // 即使是常量，仍然检查右侧表达式以发现可能的错误
    }

    // 推断右侧表达式类型
    TypeInfoPtr rhsType = inferExpressionType(expr_ctx);

    // 进行类型兼容性检查 (仅当左值非 const 且类型已知时)
    if (!lvalueIsConst && lvalueType && lvalueType != UnknownType && rhsType &&
        rhsType != UnknownType) {
      // *** 使用正确的上下文 expr_ctx 进行类型检查 ***
      if (!checkTypeAssignable(rhsType, lvalueType, expr_ctx)) {
        PLOGW << "赋值类型不匹配 (索引 " << i << "): " << rhsType->toString() << " -> "
              << lvalueType->toString() << " 在: " << ctx->getText();
        // checkTypeAssignable 内部应该已经添加了诊断
      } else {
        PLOGV << "赋值类型匹配 (索引 " << i << "): " << rhsType->toString() << " -> "
              << lvalueType->toString();
      }
    } else if (!lvalueIsConst) {
      PLOGV << "跳过赋值类型检查 (索引 " << i << ")，因为左值或右值类型未知。";
    }
  }

  // --- 2. 处理多余的左值 (用 null 填充) ---
  if (numLvalues > numExpressions) {
    PLOGW << " -> 值数量不足，" << (numLvalues - numExpressions) << " 个左值将被赋为 null。";
    for (size_t i = commonCount; i < numLvalues; ++i) {
      LangParser::LvalueContext *lval_ctx = lvalues[i];
      if (!lval_ctx)
        continue;

      TypeInfoPtr lvalueType = nullptr;
      bool lvalueIsConst = false;
      try {
        std::any lvalueResult = visit(lval_ctx); // 需要重新访问或缓存结果
        lvalueType = std::any_cast<TypeInfoPtr>(lvalueResult);
        if (!lvalueType)
          lvalueType = UnknownType;

        // --- 再次检查 const (与上面类似) ---
        if (auto lvalueBaseCtx = dynamic_cast<LangParser::LvalueBaseContext *>(lval_ctx)) {
          if (lvalueBaseCtx->IDENTIFIER()) {
            auto lvalueBaseSymbol =
                symbolTable.resolveSymbol(lvalueBaseCtx->IDENTIFIER()->getText());
            if (lvalueBaseSymbol && lvalueBaseSymbol->isConst)
              lvalueIsConst = true;
          }
        }
        // --- 结束 const 检查 ---

      } catch (...) { /* 忽略错误 */
      }

      if (lvalueIsConst) {
        addDiagnostic(getRange(lval_ctx),
                      "不能给常量 '" + lval_ctx->getText() + "' 赋 null (隐式赋值)",
                      DiagnosticSeverity::Error);
      } else if (lvalueType && lvalueType != UnknownType && !lvalueType->isEqualTo(*AnyType)) {
        // 检查 null 是否可以赋给这个左值类型 (如果不是 any)
        // NullType 是全局静态常量
        if (!checkTypeAssignable(NullType, lvalueType, lval_ctx)) {
          addDiagnostic(getRange(lval_ctx),
                        "无法将隐式 null 赋给类型 '" + lvalueType->toString() + "' 的变量 '" +
                            lval_ctx->getText() + "'",
                        DiagnosticSeverity::Error);
        }
      } else {
        // 如果左值类型是 Unknown 或 Any，通常允许赋 null
        PLOGV << " -> 允许将隐式 null 赋给未知或 any 类型的左值 (索引 " << i << ")";
      }
    }
  }

  // --- 3. 处理多余的右值 (被丢弃) ---
  if (numExpressions > numLvalues) {
    PLOGW << " -> 表达式数量 (" << numExpressions << ") 多于左值数量 (" << numLvalues
          << ")，多余的值将被丢弃。";
    // 可以考虑添加警告，特别是当最后一个表达式不是预期返回多个值的函数调用时
    // LangParser::ExpressionContext* lastExprCtx = expressions.back();
    // Range warnRange = getRange(expressions[numLvalues]); // 从第一个多余的表达式开始
    // addDiagnostic(warnRange, "赋值时有多余的表达式结果被丢弃", DiagnosticSeverity::Warning);
  }

  // 赋值语句本身不返回值
  return std::any();
}

std::any SemanticAnalyzerVisitor::visitUpdateAssignStmt(LangParser::UpdateAssignStmtContext *ctx) {
  PLOGV << "visitUpdateAssignStmt: " << ctx->getText();
  std::any lvalueResult = visit(ctx->lvalue());
  TypeInfoPtr lvalueType = nullptr;
  bool lvalueIsConst = false;
  try {
    lvalueType = std::any_cast<TypeInfoPtr>(lvalueResult);
    if (auto lvalueBaseCtx = dynamic_cast<LangParser::LvalueBaseContext *>(ctx->lvalue())) {
      if (lvalueBaseCtx->IDENTIFIER()) {
        auto lvalueSymbol = symbolTable.resolveSymbol(lvalueBaseCtx->IDENTIFIER()->getText());
        if (lvalueSymbol && lvalueSymbol->isConst) {
          lvalueIsConst = true;
        }
      }
    }
  } catch (...) {
    addDiagnostic(getRange(ctx->lvalue()), "无效的更新赋值左侧", DiagnosticSeverity::Error);
    visit(ctx->expression());
    return std::any();
  }
  TypeInfoPtr rhsType = UnknownType;
  if (lvalueType) {
    if (lvalueIsConst) {
      addDiagnostic(getRange(ctx->lvalue()), "不能更新常量 '" + ctx->lvalue()->getText() + "'",
                    DiagnosticSeverity::Error);
    }
    rhsType = inferExpressionType(ctx->expression());
  } else {
    inferExpressionType(ctx->expression());
    return std::any();
  }
  if (lvalueType != UnknownType && rhsType != UnknownType) {
    std::string opText = ctx->op->getText();
    TypeInfoPtr resultType = UnknownType; // TODO: 推断 resultType = inferBinaryOperationType(...)
    if (resultType == UnknownType) {
      // *** 使用 getTokenRange ***
      addDiagnostic(getTokenRange(ctx->op),
                    "操作符 '" + opText + "' 不支持类型 '" + lvalueType->toString() + "' 和 '" +
                        rhsType->toString() + "'",
                    DiagnosticSeverity::Error);
    } else {
      if (!checkTypeAssignable(resultType, lvalueType, ctx)) {
        addDiagnostic(getRange(ctx),
                      "更新赋值的结果类型 '" + resultType->toString() + "' 与左值类型 '" +
                          lvalueType->toString() + "' 不兼容",
                      DiagnosticSeverity::Error);
      } else {
        PLOGV << "更新赋值类型匹配: " << lvalueType->toString() << " " << opText << " "
              << rhsType->toString() << " -> " << resultType->toString();
      }
    }
  } else {
    PLOGV << "跳过更新赋值类型检查，因为左值或右值类型未知。";
  }
  return std::any();
}

// --- 左值处理 ---

/**
 * @brief 访问左值基础部分（可以是一个标识符或 this，后面可能跟随后缀）。
 * @param ctx LvalueBase 上下文。
 * @return std::any 包含最终左值的推断类型 TypeInfoPtr。
 */
std::any SemanticAnalyzerVisitor::visitLvalueBase(LangParser::LvalueBaseContext *ctx) {
  TypeInfoPtr currentType = UnknownType;
  antlr4::tree::ParseTree *lastNode = nullptr; // 用于记录处理到的最后一个节点，主要用于日志或调试
  antlr4::tree::TerminalNode *baseIdentifierNode = nullptr;
  antlr4::tree::TerminalNode *thisNode = nullptr;

  // --- 1. 处理基础部分 (IDENTIFIER 或 THIS) ---
  if (ctx->IDENTIFIER()) {
    baseIdentifierNode = ctx->IDENTIFIER();
    // 增加 nullptr 检查
    if (!baseIdentifierNode) {
      addDiagnostic(getRange(ctx), "内部错误：Lvalue 基础缺少标识符", DiagnosticSeverity::Error);
      return std::any(UnknownType);
    }
    std::string baseName = baseIdentifierNode->getText();
    PLOGV << "visitLvalueBase: 处理基础标识符 '" << baseName << "'";
    SymbolInfoPtr symbol = symbolTable.resolveSymbol(baseName);
    if (!symbol) {
      addDiagnostic(getRange(baseIdentifierNode), "未声明的标识符 '" + baseName + "'",
                    DiagnosticSeverity::Error);
      currentType = UnknownType;
    } else {
      currentType = symbol->type ? symbol->type : UnknownType; // 获取类型，可能是 nullptr
      // --- 映射基础标识符的使用点 ---
      analysisResult.nodeSymbolMap[baseIdentifierNode] = symbol;
      // --- 结束映射 ---
      PLOGV << " -> 找到符号 '" << baseName
            << "'，类型: " << (currentType ? currentType->toString() : "<null>");
    }
    lastNode = baseIdentifierNode;
  } else {
    // 理论上根据语法不会到这里，但也进行检查
    PLOGE << "内部错误：LvalueBase 既不是 IDENTIFIER 也不是 THIS";
    addDiagnostic(getRange(ctx), "内部错误：无效的 Lvalue 基础", DiagnosticSeverity::Error);
    return std::any(UnknownType);
  }

  // --- 2. 迭代处理后缀部分 ---
  // 只有在基础部分类型有效的情况下才继续处理后缀
  if (currentType && currentType != UnknownType) {
    for (auto suffixCtx : ctx->lvalueSuffix()) {
      // 增加 nullptr 检查
      if (!suffixCtx) {
        PLOGW << "  遇到空的 Lvalue 后缀节点，跳过。";
        currentType = UnknownType; // 后续处理中断
        break;
      }

      PLOGV << "  处理 Lvalue 后缀: " << suffixCtx->getText()
            << "，当前类型: " << currentType->toString();
      std::any suffixResult = visitLvalueSuffix(suffixCtx, currentType); // 调用辅助函数处理后缀

      // 更新当前类型为后缀处理后的结果类型
      try {
        currentType = std::any_cast<TypeInfoPtr>(suffixResult);
        if (!currentType) {
          PLOGV << "  -> 后缀处理返回空类型指针，类型设为 Unknown";
          currentType = UnknownType;
        } else {
          PLOGV << "  -> 后缀处理后类型变为: " << currentType->toString();
        }
      } catch (const std::bad_any_cast &e) {
        PLOGE << "  -> 内部错误: visitLvalueSuffix 返回类型不是 TypeInfoPtr. Error: " << e.what();
        currentType = UnknownType;
      } catch (const std::exception &e) {
        PLOGE << "  -> 处理后缀时出错: " << e.what();
        currentType = UnknownType;
      }

      // 如果后缀处理导致类型未知，则停止进一步处理后缀
      if (currentType == UnknownType) {
        PLOGV << "  -> 因后缀处理后类型未知，停止后续后缀处理。";
        break;
      }
      lastNode = suffixCtx; // 更新 lastNode 以指向最后一个处理的后缀节点
    }
  } else {
    PLOGV << "Lvalue 基础部分类型未知，跳过后缀处理。";
  }

  PLOGV << "visitLvalueBase 结束，最终左值类型: "
        << (currentType ? currentType->toString() : "<null>");
  // 返回最终推断出的整个左值表达式的类型
  return std::any(currentType);
}

/**
 * @brief 访问左值后缀（例如 .member 或 [index]）。
 * 这是处理 Lvalue 链式访问的关键辅助函数。
 * @param suffixNode 后缀节点上下文。
 * @param baseType 基础表达式的推断类型。
 * @return std::any 包含后缀操作后的结果类型 TypeInfoPtr。
 */
std::any SemanticAnalyzerVisitor::visitLvalueSuffix(LangParser::LvalueSuffixContext *suffixNode,
                                                    const TypeInfoPtr &baseType) {
  // 增加 nullptr 检查
  if (!suffixNode || !baseType || baseType == UnknownType) {
    PLOGW << "visitLvalueSuffix 收到无效参数或未知基础类型，停止处理。";
    return std::any(UnknownType);
  }
  PLOGV << "visitLvalueSuffix on " << baseType->toString();

  if (auto indexCtx = dynamic_cast<LangParser::LvalueIndexContext *>(suffixNode)) {
    PLOGV << " -> Lvalue 索引访问: [" << indexCtx->expression()->getText() << "]";
    // 索引访问的类型推断逻辑
    TypeInfoPtr indexExprType = inferExpressionType(indexCtx->expression());
    TypeInfoPtr resultType = UnknownType;
    if (baseType->isEqualTo(*AnyType) || indexExprType->isEqualTo(*AnyType)) {
      resultType = AnyType;
    } else if (auto listType = std::dynamic_pointer_cast<ListType>(baseType)) {
      // 允许整数或 number 作为列表索引
      if (checkTypeAssignable(indexExprType, IntType, indexCtx->expression()) ||
          checkTypeAssignable(indexExprType, NumberType, indexCtx->expression())) {
        resultType = listType->elementType ? listType->elementType : AnyType; // 列表元素类型
        PLOGV << "   -> 列表索引结果类型: " << resultType->toString();
      } else {
        addDiagnostic(getRange(indexCtx->expression()),
                      "列表索引必须是整数或数字类型，实际为 '" + indexExprType->toString() + "'",
                      DiagnosticSeverity::Error);
      }
    } else if (auto mapType = std::dynamic_pointer_cast<MapType>(baseType)) {
      // 检查索引类型是否与 Map 的键类型兼容
      if (checkTypeAssignable(indexExprType, mapType->keyType, indexCtx->expression())) {
        resultType = mapType->valueType ? mapType->valueType : AnyType; // Map 值类型
        PLOGV << "   -> 映射索引结果类型: " << resultType->toString();
      } else {
        addDiagnostic(getRange(indexCtx->expression()),
                      "映射键类型 '" + indexExprType->toString() + "' 与期望的键类型 '" +
                          mapType->keyType->toString() + "' 不兼容",
                      DiagnosticSeverity::Error);
      }
    } else if (auto stringType = std::dynamic_pointer_cast<BaseType>(baseType);
               stringType && stringType->kind == BaseTypeKind::STRING) {
      // 允许对字符串进行索引（假设返回 string 或 char 类型，根据语言设计）
      if (checkTypeAssignable(indexExprType, IntType, indexCtx->expression()) ||
          checkTypeAssignable(indexExprType, NumberType, indexCtx->expression())) {
        resultType = StringType; // 或者一个 CharType 如果有的话
        PLOGV << "   -> 字符串索引结果类型: " << resultType->toString();
      } else {
        addDiagnostic(getRange(indexCtx->expression()),
                      "字符串索引必须是整数或数字类型，实际为 '" + indexExprType->toString() + "'",
                      DiagnosticSeverity::Error);
      }
    } else {
      addDiagnostic(getRange(indexCtx), "类型 '" + baseType->toString() + "' 不支持索引赋值 '[]'",
                    DiagnosticSeverity::Error);
    }
    return std::any(resultType);
  } else if (auto memberCtx = dynamic_cast<LangParser::LvalueMemberContext *>(suffixNode)) {
    antlr4::tree::TerminalNode *memberIdentifierNode = memberCtx->IDENTIFIER();
    // 增加 nullptr 检查
    if (!memberIdentifierNode) {
      addDiagnostic(getRange(memberCtx), "内部错误：成员访问缺少标识符", DiagnosticSeverity::Error);
      return std::any(UnknownType);
    }
    std::string memberName = memberIdentifierNode->getText();
    TypeInfoPtr resultType = UnknownType;
    PLOGV << " -> Lvalue 成员访问: " << memberName;

    if (baseType->isEqualTo(*AnyType)) {
      PLOGV << "   -> 基础类型为 any，成员类型为 any";
      resultType = AnyType;
    } else if (auto classType = std::dynamic_pointer_cast<ClassType>(baseType)) {
      if (classType->classScope) {
        SymbolInfoPtr memberSymbol =
            classType->classScope->resolveLocally(memberName); // 在类作用域本地查找
        if (memberSymbol) {
          if (memberSymbol->isStatic) {
            addDiagnostic(getRange(memberIdentifierNode),
                          "不能通过实例访问/赋值静态成员 '" + memberName + "'",
                          DiagnosticSeverity::Error);
            // 仍然返回类型，但标记错误
          }
          resultType = memberSymbol->type ? memberSymbol->type : UnknownType;
          // --- **关键: 填充成员标识符节点的映射** ---
          analysisResult.nodeSymbolMap[memberIdentifierNode] = memberSymbol;
          // --- 结束填充 ---
          PLOGV << "   -> 在类 '" << classType->name
                << "' 中找到成员，类型: " << (resultType ? resultType->toString() : "<null>");
        } else {
          addDiagnostic(getRange(memberIdentifierNode),
                        "类 '" + classType->name + "' 中没有名为 '" + memberName + "' 的成员",
                        DiagnosticSeverity::Error);
        }
      } else {
        addDiagnostic(getRange(memberCtx),
                      "内部错误：类 '" + classType->name + "' 没有关联的作用域",
                      DiagnosticSeverity::Error);
      }
    }
    // TODO: 处理其他可能支持成员访问的基础类型或模块类型 (例如 namespace import)
    else {
      addDiagnostic(getRange(memberCtx), "类型 '" + baseType->toString() + "' 不支持成员访问 '.'",
                    DiagnosticSeverity::Error);
    }
    return std::any(resultType); // 返回成员类型
  } else {
    PLOGE << "未知的左值后缀类型";
    return std::any(UnknownType);
  }
}

// src/semantic_analyzer_visitor.cpp

// --- Part 1, 2, 3 code should be here ---
// ...

// ============================================================================
// Part 4: 二元运算符与基本控制流 (Binary Operators & Basic Control Flow)
// ============================================================================

// --- 二元运算符辅助函数 (需要添加到类声明 private 部分) ---

/**
 * @brief 推断二元运算结果类型 (需要根据 SptScript 操作符规则详细实现)
 * @param leftType 左操作数类型
 * @param op 操作符文本 (e.g., "+", "==", "&&")
 * @param rightType 右操作数类型
 * @param ctx 用于报告错误的上下文节点
 * @return 结果类型 TypeInfoPtr，若操作无效则返回 UnknownType
 */
TypeInfoPtr SemanticAnalyzerVisitor::inferBinaryOperationType(const TypeInfoPtr &leftType,
                                                              const std::string &op,
                                                              const TypeInfoPtr &rightType,
                                                              antlr4::ParserRuleContext *ctx) {
  if (!leftType || !rightType || leftType == UnknownType || rightType == UnknownType ||
      leftType->isEqualTo(*AnyType) || rightType->isEqualTo(*AnyType)) {
    // 如果任一操作数类型未知或是 any，结果通常是 any (或 Unknown)
    PLOGV << "二元运算涉及 Unknown/Any 类型，结果假定为 Any: " << ctx->getText();
    // 对于比较操作符，即使有 any，结果也应该是 bool
    if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=")
      return BoolType;
    if (op == "&&" || op == "||")
      return BoolType; // 逻辑运算结果也是 bool
    return AnyType;    // 其他运算结果可能是 any
  }

  PLOGV << "推断二元运算类型: " << leftType->toString() << " " << op << " "
        << rightType->toString();

  // --- 算术运算符 (+, -, *, /, %) ---
  if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
    bool leftIsNum = leftType->isEqualTo(*IntType) || leftType->isEqualTo(*FloatType) ||
                     leftType->isEqualTo(*NumberType);
    bool rightIsNum = rightType->isEqualTo(*IntType) || rightType->isEqualTo(*FloatType) ||
                      rightType->isEqualTo(*NumberType);

    if (leftIsNum && rightIsNum) {
      // 如果两边都是数字
      if (leftType->isEqualTo(*FloatType) || rightType->isEqualTo(*FloatType)) {
        PLOGV << " -> 算术结果: float";
        return FloatType; // 只要有一个是 float，结果就是 float
      }
      if (leftType->isEqualTo(*NumberType) || rightType->isEqualTo(*NumberType)) {
        PLOGV << " -> 算术结果: number";
        return NumberType; // 涉及 number，结果是 number
      }
      PLOGV << " -> 算术结果: int";
      return IntType; // 否则结果是 int
    } else {
      addDiagnostic(getRange(ctx),
                    "算术运算符 '" + op + "' 只能用于数字类型，不能用于 '" + leftType->toString() +
                        "' 和 '" + rightType->toString() + "'",
                    DiagnosticSeverity::Error);
      return UnknownType;
    }
  }

  // --- 字符串连接 (..) ---
  if (op == "..") {
    if (checkTypeAssignable(leftType, StringType, ctx) &&
        checkTypeAssignable(rightType, StringType, ctx)) {
      PLOGV << " -> 字符串连接结果: string";
      return StringType;
    } else {
      addDiagnostic(getRange(ctx), "字符串连接 '..' 操作符只能用于字符串类型",
                    DiagnosticSeverity::Error);
      return UnknownType;
    }
  }

  // --- 比较运算符 (<, >, <=, >=) ---
  if (op == "<" || op == ">" || op == "<=" || op == ">=") {
    // 通常只允许数字或字符串之间比较
    bool leftIsComparable = leftType->isEqualTo(*IntType) || leftType->isEqualTo(*FloatType) ||
                            leftType->isEqualTo(*NumberType) || leftType->isEqualTo(*StringType);
    bool rightIsComparable = rightType->isEqualTo(*IntType) || rightType->isEqualTo(*FloatType) ||
                             rightType->isEqualTo(*NumberType) || rightType->isEqualTo(*StringType);
    // 并且两边类型需要兼容（都是数字或都是字符串）
    bool typesCompatible = ((leftType->isEqualTo(*IntType) || leftType->isEqualTo(*FloatType) ||
                             leftType->isEqualTo(*NumberType)) &&
                            (rightType->isEqualTo(*IntType) || rightType->isEqualTo(*FloatType) ||
                             rightType->isEqualTo(*NumberType))) ||
                           (leftType->isEqualTo(*StringType) && rightType->isEqualTo(*StringType));

    if (leftIsComparable && rightIsComparable && typesCompatible) {
      PLOGV << " -> 比较运算结果: bool";
      return BoolType; // 比较结果总是布尔值
    } else {
      addDiagnostic(getRange(ctx),
                    "比较运算符 '" + op + "' 不能用于类型 '" + leftType->toString() + "' 和 '" +
                        rightType->toString() + "' 或类型不兼容",
                    DiagnosticSeverity::Error);
      return UnknownType;
    }
  }

  // --- 相等性运算符 (==, !=) ---
  if (op == "==" || op == "!=") {
    // SptScript 可能允许更多类型比较（如 null, boolean, list?, map?），需要根据规范确定
    // 简化：允许基本类型、null、list、map、function、coro 之间比较（与同类型或 null）
    bool comparable = false;
    if (leftType->isEqualTo(*NullType) || rightType->isEqualTo(*NullType))
      comparable = true; // null 可以和任何类型比较? (取决于语言设计)
    else if (leftType->isEqualTo(*BoolType) && rightType->isEqualTo(*BoolType))
      comparable = true;
    else if ((leftType->isEqualTo(*IntType) || leftType->isEqualTo(*FloatType) ||
              leftType->isEqualTo(*NumberType)) &&
             (rightType->isEqualTo(*IntType) || rightType->isEqualTo(*FloatType) ||
              rightType->isEqualTo(*NumberType)))
      comparable = true;
    else if (leftType->isEqualTo(*StringType) && rightType->isEqualTo(*StringType))
      comparable = true;
    // TODO: 添加对 list, map, function, coro, class 实例的比较规则 (通常是引用比较或不支持)
    // else if (std::dynamic_pointer_cast<ListType>(leftType) &&
    // std::dynamic_pointer_cast<ListType>(rightType)) comparable = true; // 假设可比较
    // ...

    if (comparable) {
      PLOGV << " -> 相等性运算结果: bool";
      return BoolType;
    } else {
      addDiagnostic(getRange(ctx),
                    "相等性运算符 '" + op + "' 不能用于不兼容的类型 '" + leftType->toString() +
                        "' 和 '" + rightType->toString() + "'",
                    DiagnosticSeverity::Warning); // 可能只是警告
      return BoolType;                            // 即使类型可能不匹配，结果也应该是 bool
    }
  }

  // --- 逻辑运算符 (&&, ||) ---
  if (op == "&&" || op == "||") {
    // 操作数通常需要是布尔类型，或者能隐式转换为布尔值
    // 假设 SptScript 遵循 Lua 规则：nil 和 false 为假，其他为真
    // 静态类型检查时，可以要求 bool 类型，或者允许 any
    // if (checkTypeAssignable(leftType, BoolType, ctx) && checkTypeAssignable(rightType, BoolType,
    // ctx)) {
    PLOGV << " -> 逻辑运算结果: bool";
    return BoolType; // 逻辑运算结果总是布尔值
                     // } else {
    //     addDiagnostic(getRange(ctx), "逻辑运算符 '" + op + "' 的操作数应为布尔类型",
    //     DiagnosticSeverity::Warning); return BoolType; // 结果仍是 bool
    // }
  }

  // --- 位运算符 (&, |, ^, <<, >>) ---
  if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>") {
    // 通常只允许整数类型
    if ((leftType->isEqualTo(*IntType) || leftType->isEqualTo(*AnyType)) &&
        (rightType->isEqualTo(*IntType) || rightType->isEqualTo(*AnyType))) {
      PLOGV << " -> 位运算结果: int";
      return IntType; // 位运算结果是 int (或 any)
    } else {
      addDiagnostic(getRange(ctx), "位运算符 '" + op + "' 只能用于整数类型",
                    DiagnosticSeverity::Error);
      return UnknownType;
    }
  }

  PLOGW << "未处理的二元操作符 '" << op << "' 对于类型 " << leftType->toString() << " 和 "
        << rightType->toString();
  addDiagnostic(getRange(ctx), "不支持的操作符 '" + op + "' 用于给定类型",
                DiagnosticSeverity::Error);
  return UnknownType; // 未知或不支持的操作
}

// --- 二元运算符表达式 Visit 方法 (部分实现) ---

// 辅助宏，用于简化二元表达式访问器的实现
#define VISIT_BINARY_OPERATOR(NodeName, OpSymbol)                                                  \
  std::any SemanticAnalyzerVisitor::visit##NodeName(LangParser::NodeName##Context *ctx) {          \
    PLOGV << "visit" #NodeName ": " << ctx->getText();                                             \
    if (ctx->children.size() < 3) {                                                                \
      /* 如果只有一个子节点，说明没有发生二元运算，直接访问子节点 */                               \
      return visit(ctx->children[0]);                                                              \
    }                                                                                              \
    /* 处理二元运算 */                                                                             \
    TypeInfoPtr leftType = UnknownType;                                                            \
    std::any leftResult = visit(ctx->children[0]);                                                 \
    try {                                                                                          \
      leftType = std::any_cast<TypeInfoPtr>(leftResult);                                           \
      if (!leftType)                                                                               \
        leftType = UnknownType;                                                                    \
    } catch (...) { /* 忽略 */                                                                     \
    }                                                                                              \
                                                                                                   \
    TypeInfoPtr finalType = leftType; /* 初始类型为最左操作数类型 */                               \
    /* 迭代处理所有操作符和右操作数 */                                                             \
    for (size_t i = 1; i < ctx->children.size(); i += 2) {                                         \
      if (finalType == UnknownType)                                                                \
        break; /* 如果中间类型未知，停止推断 */                                                    \
      antlr4::tree::ParseTree *opNode = ctx->children[i];                                          \
      antlr4::tree::ParseTree *rightNode = ctx->children[i + 1];                                   \
      std::string opText = opNode->getText();                                                      \
                                                                                                   \
      TypeInfoPtr rightType = UnknownType;                                                         \
      std::any rightResult = visit(rightNode);                                                     \
      try {                                                                                        \
        rightType = std::any_cast<TypeInfoPtr>(rightResult);                                       \
        if (!rightType)                                                                            \
          rightType = UnknownType;                                                                 \
      } catch (...) { /* 忽略 */                                                                   \
      }                                                                                            \
                                                                                                   \
      /* 推断当前操作的结果类型 */                                                                 \
      finalType = inferBinaryOperationType(finalType, opText, rightType,                           \
                                           dynamic_cast<ParserRuleContext *>(ctx));                \
    }                                                                                              \
    return std::any(finalType);                                                                    \
  }

// 使用宏为每个二元运算规则生成 visit 方法
VISIT_BINARY_OPERATOR(LogicalOrExpression, "||")

VISIT_BINARY_OPERATOR(LogicalAndExpression, "&&")

VISIT_BINARY_OPERATOR(BitwiseOrExpression, "|")

VISIT_BINARY_OPERATOR(BitwiseXorExpression, "^")

VISIT_BINARY_OPERATOR(BitwiseAndExpression, "&")

VISIT_BINARY_OPERATOR(EqualityExpression, "==" / "!=")               // 操作符在 infer 中处理
VISIT_BINARY_OPERATOR(ComparisonExpression, "<" / ">" / "<=" / ">=") // 操作符在 infer 中处理
VISIT_BINARY_OPERATOR(ShiftExpression, "<<" / ">>")

VISIT_BINARY_OPERATOR(ConcatExpression, "..")

VISIT_BINARY_OPERATOR(AddSubExpression, "+" / "-")

VISIT_BINARY_OPERATOR(MulDivModExpression, "*" / "/" / "%")

// 清理宏定义
#undef VISIT_BINARY_OPERATOR

// --- 控制流语句 Visit 方法 ---

/**
 * @brief 访问 If 语句 (IfStmtContext)。
 * 对应 if (...) { ... } else if (...) { ... } else { ... }
 */
std::any SemanticAnalyzerVisitor::visitIfStmt(LangParser::IfStmtContext *ctx) {
  PLOGV << "visitIfStmt";
  // 直接访问其包含的 IfStatement 节点
  return visit(ctx->ifStatement());
}

/**
 * @brief 访问 If 语句的具体实现 (IfStatementContext)。
 */
std::any SemanticAnalyzerVisitor::visitIfStatement(LangParser::IfStatementContext *ctx) {
  PLOGV << "visitIfStatement";
  size_t blockIdx = 0; // 用于追踪当前处理的 blockStatement 索引

  // 处理第一个 IF
  if (!ctx->expression().empty()) {
    TypeInfoPtr conditionType = inferExpressionType(ctx->expression(0));
    if (conditionType != UnknownType && !conditionType->isEqualTo(*BoolType) &&
        !conditionType->isEqualTo(*AnyType)) {
      addDiagnostic(getRange(ctx->expression(0)),
                    "if 条件表达式类型应为 boolean，实际为 '" + conditionType->toString() + "'",
                    DiagnosticSeverity::Warning);
    }
    if (blockIdx < ctx->blockStatement().size()) { // 确保 blockIdx 有效
      visit(ctx->blockStatement(blockIdx++));
    } else {
      PLOGE << "If 语句缺少代码块";
      // *** 修正：使用 ctx->IF(0) 获取第一个 IF 关键字节点 ***
      addDiagnostic(getRange(ctx->IF(0)), "if 语句缺少代码块", DiagnosticSeverity::Error);
    }
  } else {
    PLOGE << "If 语句缺少条件表达式";
    // *** 修正：使用 ctx->IF(0) 获取第一个 IF 关键字节点 ***
    addDiagnostic(getRange(ctx->IF(0)), "if 语句缺少条件表达式", DiagnosticSeverity::Error);
    return std::any(); // 没有条件，无法继续
  }

  // 处理 ELSE IF 部分
  for (size_t i = 1; i < ctx->expression().size(); ++i) {
    TypeInfoPtr conditionType = inferExpressionType(ctx->expression(i));
    if (conditionType != UnknownType && !conditionType->isEqualTo(*BoolType) &&
        !conditionType->isEqualTo(*AnyType)) {
      addDiagnostic(getRange(ctx->expression(i)),
                    "else if 条件表达式类型应为 boolean，实际为 '" + conditionType->toString() +
                        "'",
                    DiagnosticSeverity::Warning);
    }
    if (blockIdx < ctx->blockStatement().size()) {
      visit(ctx->blockStatement(blockIdx++));
    } else {
      PLOGE << "else if 语句缺少代码块 (表达式索引 " << i << ")";
      // 找到对应的 ELSE IF 关键字比较困难，暂时还是用 expression 的位置
      addDiagnostic(getRange(ctx->expression(i)), "else if 语句缺少代码块",
                    DiagnosticSeverity::Error);
    }
  }

  // 处理 ELSE 部分
  // *** 修正：检查 !ctx->ELSE().empty() ***
  if (!ctx->ELSE().empty()) {
    PLOGV << " -> 处理 else 分支";
    if (blockIdx < ctx->blockStatement().size()) {
      visit(ctx->blockStatement(blockIdx));
    } else {
      PLOGE << "else 语句缺少代码块";
      // *** 修正：使用 ctx->ELSE(0) 获取第一个 ELSE 关键字节点 ***
      if (!ctx->ELSE().empty())
        addDiagnostic(getRange(ctx->ELSE(0)), "else 语句缺少代码块", DiagnosticSeverity::Error);
    }
  }

  return std::any();
}

/**
 * @brief 访问 While 语句 (WhileStmtContext)。
 */
std::any SemanticAnalyzerVisitor::visitWhileStmt(LangParser::WhileStmtContext *ctx) {
  PLOGV << "visitWhileStmt";
  // 直接访问其包含的 WhileStatement 节点
  return visit(ctx->whileStatement());
}

/**
 * @brief 访问 While 语句的具体实现 (WhileStatementContext)。
 */
std::any SemanticAnalyzerVisitor::visitWhileStatement(LangParser::WhileStatementContext *ctx) {
  PLOGV << "visitWhileStatement";
  // 1. 检查条件表达式类型
  TypeInfoPtr conditionType = inferExpressionType(ctx->expression());
  if (conditionType != UnknownType && !conditionType->isEqualTo(*BoolType) &&
      !conditionType->isEqualTo(*AnyType)) {
    addDiagnostic(getRange(ctx->expression()),
                  "while 条件表达式类型应为 boolean，实际为 '" + conditionType->toString() + "'",
                  DiagnosticSeverity::Warning); // 或 Error
  }

  // 2. 访问循环体代码块
  if (!ctx->blockStatement()) {
    return std::any();
  }

  visit(ctx->blockStatement());

  return std::any();
}

/**
 * @brief 访问 Break 语句 (BreakStmtContext)。
 */
std::any SemanticAnalyzerVisitor::visitBreakStmt(LangParser::BreakStmtContext *ctx) {
  PLOGV << "visitBreakStmt";
  // TODO: 检查 break 是否在循环内部 (需要跟踪循环嵌套层数)
  // if (loopDepth == 0) { addDiagnostic(getRange(ctx), "'break' 只能出现在循环内部"); }
  return std::any();
}

/**
 * @brief 访问 Continue 语句 (ContinueStmtContext)。
 */
std::any SemanticAnalyzerVisitor::visitContinueStmt(LangParser::ContinueStmtContext *ctx) {
  PLOGV << "visitContinueStmt";
  // TODO: 检查 continue 是否在循环内部
  // if (loopDepth == 0) { addDiagnostic(getRange(ctx), "'continue' 只能出现在循环内部"); }
  return std::any();
}

/**
 * @brief 访问 Return 语句 (ReturnStmtContext)。
 */
std::any SemanticAnalyzerVisitor::visitReturnStmt(LangParser::ReturnStmtContext *ctx) {
  PLOGV << "visitReturnStmt";

  // 1. 检查是否在函数或方法内部
  if (expectedReturnTypeStack.empty()) {
    addDiagnostic(getRange(ctx), "'return' 语句只能出现在函数或方法内部",
                  DiagnosticSeverity::Error);
    // 仍然尝试访问表达式以发现其他错误
    if (ctx->expressionList()) {
      visit(ctx->expressionList());
    }
    return std::any();
  }

  // 2. 获取当前函数预期的返回类型
  TypeInfoPtr expectedType = expectedReturnTypeStack.top(); // 获取栈顶类型
  bool expectsMultiReturn = (expectedType == nullptr);      // 用 nullptr 代表 mutivar

  // 3. 获取实际返回的类型
  std::vector<TypeInfoPtr> returnedTypes;
  if (ctx->expressionList()) {
    // 访问表达式列表以获取返回类型
    std::any exprListResult = visit(ctx->expressionList());
    try {
      returnedTypes = std::any_cast<std::vector<TypeInfoPtr>>(exprListResult);
    } catch (...) {
      addDiagnostic(getRange(ctx->expressionList()), "内部错误：return 语句的表达式列表无效",
                    DiagnosticSeverity::Error);
      return std::any(); // 无法处理，直接返回
    }
  }
  // else: 没有返回表达式，returnedTypes 为空

  // 4. 进行类型检查
  if (expectsMultiReturn) {
    // 函数声明为 mutivar，可以返回任意数量、任意类型的值，或者不返回
    PLOGV << " -> 在多返回值函数中，允许返回 " << returnedTypes.size() << " 个值";
    // 静态检查无法精确判断，运行时处理
  } else {
    // 函数期望单个返回值 (或 void)
    if (returnedTypes.empty()) {
      // 实际未返回值
      if (!expectedType->isEqualTo(*VoidType)) {
        addDiagnostic(getRange(ctx),
                      "函数期望返回 '" + expectedType->toString() +
                          "' 类型的值，但 return 语句没有返回值",
                      DiagnosticSeverity::Error);
      } else {
        PLOGV << " -> return 语句无值，符合 void 返回类型";
      }
    } else if (returnedTypes.size() == 1) {
      // 实际返回单个值
      TypeInfoPtr actualType = returnedTypes[0];
      if (expectedType->isEqualTo(*VoidType)) {
        addDiagnostic(getRange(ctx),
                      "函数期望返回 void，但 return 语句返回了 '" + actualType->toString() +
                          "' 类型的值",
                      DiagnosticSeverity::Error);
      } else {
        if (!checkTypeAssignable(actualType, expectedType, ctx->expressionList()->expression(0))) {
          // checkTypeAssignable 会报告错误
          PLOGW << " -> 返回类型不匹配: 期望 '" << expectedType->toString() << "', 得到 '"
                << actualType->toString() << "'";
        } else {
          PLOGV << " -> 返回类型匹配: '" << actualType->toString() << "' -> '"
                << expectedType->toString() << "'";
        }
      }
    } else {
      // 实际返回多个值，但函数不期望 mutivar
      if (expectedType->isEqualTo(*VoidType)) {
        addDiagnostic(getRange(ctx), "函数期望返回 void，但 return 语句返回了多个值",
                      DiagnosticSeverity::Error);
      } else {
        addDiagnostic(getRange(ctx),
                      "函数期望返回单个 '" + expectedType->toString() +
                          "' 类型的值，但 return 语句返回了多个值",
                      DiagnosticSeverity::Error);
      }
    }
  }

  return std::any();
}

// --- 结束 Part 4 ---

// src/semantic_analyzer_visitor.cpp

// --- Part 1, 2, 3, 4 code should be here ---
// ...

// ============================================================================
// Part 5: For 循环、字面量、New、Lambda、Import、Mutivar 等
// ============================================================================

// --- For 循环 ---

/**
 * @brief 访问 For 语句 (ForStmtContext)。(修正：通过 forStatement() 访问子节点)
 * 这是 statement 规则中 #forStmt 标签对应的 visit 方法。
 */
std::any SemanticAnalyzerVisitor::visitForStmt(LangParser::ForStmtContext *ctx) {
  PLOGV << "visitForStmt";
  ScopeGuard forScopeGuard(symbolTable, ScopeKind::BLOCK, ctx); // 创建 for 循环的块作用域
  recordScopeRange(symbolTable.getCurrentScope(), ctx);
  // TODO: 设置循环状态，用于检查 break/continue
  // bool outerLoopState = isInLoop; isInLoop = true;

  // *** 修正：通过 ctx->forStatement() 访问实际的 for 循环规则节点 ***
  auto forStatementNode = ctx->forStatement();
  if (!forStatementNode) {
    PLOGE << "内部错误: ForStmtContext 缺少 forStatement 子节点";
    addDiagnostic(getRange(ctx), "内部错误: 无法处理 for 语句", DiagnosticSeverity::Error);
    return std::any();
  }

  // 访问 for 控制结构
  if (forStatementNode->forControl()) { // 检查子节点是否存在
    visit(forStatementNode->forControl());
  } else {
    PLOGE << "错误: ForStatementContext 缺少 forControl 子节点";
    addDiagnostic(getRange(forStatementNode), "内部错误: 无法处理 for 循环控制结构",
                  DiagnosticSeverity::Error);
  }

  // 访问循环体
  if (forStatementNode->blockStatement()) { // 检查子节点是否存在
    visit(forStatementNode->blockStatement());
  } else {
    PLOGE << "错误: ForStatementContext 缺少 blockStatement 子节点";
    addDiagnostic(getRange(forStatementNode), "内部错误: 无法处理 for 循环体",
                  DiagnosticSeverity::Error);
  }

  // TODO: 恢复循环状态
  // isInLoop = outerLoopState;

  return std::any();
}

/**
 * @brief 访问 C 风格 For 循环控制 (ForCStyleControlContext)。
 * 处理 `forInitStatement ; expression? ; forUpdate?`
 */
std::any SemanticAnalyzerVisitor::visitForCStyleControl(LangParser::ForCStyleControlContext *ctx) {
  PLOGV << "visitForCStyleControl";
  // C 风格的 for 循环的初始化部分可能声明变量，其作用域仅限于整个 for 语句
  // 我们已经在 visitForStmt 创建了作用域，所以这里直接访问初始化语句
  if (ctx->forInitStatement()) {
    visit(ctx->forInitStatement()); // 访问初始化语句
  }

  // 访问条件表达式 (如果存在) 并检查类型
  if (ctx->expression()) {
    TypeInfoPtr conditionType = inferExpressionType(ctx->expression());
    if (conditionType != UnknownType && !conditionType->isEqualTo(*BoolType) &&
        !conditionType->isEqualTo(*AnyType)) {
      addDiagnostic(getRange(ctx->expression()),
                    "for 循环条件表达式类型应为 boolean，实际为 '" + conditionType->toString() +
                        "'",
                    DiagnosticSeverity::Warning);
    }
  }

  // 访问更新部分 (如果存在)
  if (ctx->forUpdate()) {
    visit(ctx->forUpdate());
  }

  return std::any();
}

/**
 * @brief 访问 For 循环初始化语句 (ForInitStatementContext)。
 */
std::any SemanticAnalyzerVisitor::visitForInitStatement(LangParser::ForInitStatementContext *ctx) {
  PLOGV << "visitForInitStatement";
  // 可能是 SptScript 多重声明、赋值语句列表、表达式列表或空
  if (ctx->multiDeclaration()) {
    return visit(ctx->multiDeclaration());
  } else if (ctx->assignStatement()) { // *** 修改: 直接检查指针是否存在 ***
    // *** 修改: 访问单个赋值语句 ***
    visit(ctx->assignStatement());
    return std::any();
  } else if (ctx->expressionList()) {
    // 访问表达式列表，推断类型但通常忽略结果
    std::any result = visit(ctx->expressionList());
    // 可以选择性检查这里的表达式是否有副作用，纯计算可能无意义
    return result; // 返回表达式列表的结果（类型向量）
  }
  // 如果以上都不是，则是空初始化语句，无操作
  return std::any();
}

/**
 * @brief 访问 SptScript 特有的多重变量声明 (MultiDeclarationContext)。
 * 处理 `declaration_item [=expr] (, declaration_item [=expr])*`
 * @return std::any 包含一个 std::vector<SymbolInfoPtr>。
 */
std::any SemanticAnalyzerVisitor::visitMultiDeclaration(LangParser::MultiDeclarationContext *ctx) {
  PLOGV << "visitSptScriptMultiDeclaration";
  std::vector<SymbolInfoPtr> definedSymbols;
  // 语法是 (declaration_item (= expression)?) (COMMA declaration_item (= expression)?)*
  // 需要正确匹配 declaration_item 和对应的可选 expression
  auto declItems = ctx->declaration_item();
  auto expressions = ctx->expression();
  size_t exprIdx = 0;

  for (auto declItemCtx : declItems) {
    // 1. 获取标识符名称和位置
    antlr4::tree::TerminalNode *identifierNode = declItemCtx->IDENTIFIER();
    std::string varName = identifierNode->getText();
    Location defLoc = getLocation(declItemCtx->IDENTIFIER());

    // 2. 确定变量类型
    TypeInfoPtr varType = UnknownType;
    antlr4::Token *autoToken = nullptr; // 用于 auto 错误定位
    if (declItemCtx->type()) {
      varType = resolveTypeFromContext(declItemCtx->type());
      if (!varType || varType == UnknownType) { /* 错误处理 */
        varType = UnknownType;
      }
      if (varType->isEqualTo(*VoidType)) {
        addDiagnostic(getRange(declItemCtx->type()), "不能声明 void 类型的变量 '" + varName + "'",
                      DiagnosticSeverity::Error);
        varType = UnknownType;
      }
    } else if (declItemCtx->AUTO()) {
      autoToken = declItemCtx->AUTO()->getSymbol();
      // auto 类型需要在后面根据初始化表达式确定
      varType = UnknownType; // 暂时设为未知
    } else {                 /* 语法错误 */
      varType = UnknownType;
    }

    // 3. 创建符号信息 (类型可能稍后更新)
    SymbolInfoPtr varSymbol = std::make_shared<SymbolInfo>(
        varName, SymbolKind::VARIABLE, varType, defLoc, symbolTable.getCurrentScope(), declItemCtx);
    varSymbol->isConst = false;  // for 循环初始化不允许 const
    varSymbol->isGlobal = false; // for 循环初始化是局部的

    // 4. 定义符号
    if (!symbolTable.defineSymbol(varSymbol)) {
      // 处理重定义错误
      SymbolInfoPtr existingSymbol = symbolTable.getCurrentScope()->resolveLocally(varName);
      std::string errorMsg = "变量 '" + varName + "' 在 for 初始化中重复定义";
      if (existingSymbol) {
        errorMsg += "。先前定义在 line " +
                    std::to_string(existingSymbol->definitionLocation.range.start.line + 1);
      }
      addDiagnostic(getRange(declItemCtx->IDENTIFIER()), errorMsg, DiagnosticSeverity::Error);
      continue; // 跳过处理此变量的初始化
    } else {
      definedSymbols.push_back(varSymbol);
      analysisResult.nodeSymbolMap[identifierNode] = varSymbol;
      PLOGV << "成功定义 for 初始化变量 '" << varName << "' (类型待定或已知)";
    }

    // 5. 处理对应的初始化表达式 (如果存在)
    // 检查当前 declItem 是否有对应的 expression
    // 注意：ANTLR 生成的 ctx->expression() 列表可能与 ctx->declaration_item() 列表不完全对应，
    // 需要通过检查 ASSIGN 标记来判断。语法是 declaration_item (ASSIGN expression)?
    // 更可靠的方式是检查 declItemCtx 的子节点数量或结构。
    // 简化：假设如果 expressions 列表还有元素，就对应当前的 declItem
    bool hasInitializer = (exprIdx < expressions.size()); // 非常粗略的假设！

    if (hasInitializer) {
      LangParser::ExpressionContext *exprCtx = expressions[exprIdx++];
      TypeInfoPtr initExprType = inferExpressionType(exprCtx);

      if (autoToken) { // 如果是 auto 类型
        if (!initExprType || initExprType == UnknownType) {
          addDiagnostic(getRange(exprCtx),
                        "无法从初始化表达式推断 'auto' 变量 '" + varName + "' 的类型",
                        DiagnosticSeverity::Warning);
          varSymbol->type = AnyType; // 推断失败，默认为 any?
        } else if (initExprType->isEqualTo(*VoidType)) {
          addDiagnostic(getRange(exprCtx),
                        "不能用返回 void 的表达式初始化 'auto' 变量 '" + varName + "'",
                        DiagnosticSeverity::Error);
          varSymbol->type = UnknownType;
        } else {
          varSymbol->type = initExprType; // 更新符号类型
          PLOGV << " -> auto 变量 '" << varName << "' 推断类型为: " << initExprType->toString();
        }
      } else {                                              // 如果是显式类型
        if (!initExprType || initExprType == UnknownType) { /* 警告 */
        } else if (varSymbol->type != UnknownType &&
                   !checkTypeAssignable(initExprType, varSymbol->type, exprCtx)) {
          // 类型不匹配
          PLOGW << "for 初始化类型不匹配: '" << initExprType->toString() << "' -> '"
                << varSymbol->type->toString() << "'";
        }
      }
    } else if (autoToken) {
      // auto 变量没有初始化器
      addDiagnostic(getTokenRange(autoToken),
                    "'auto' 变量 '" + varName + "' 在 for 初始化中必须被初始化",
                    DiagnosticSeverity::Error);
      varSymbol->type = UnknownType;
    }
    // 没有初始化器的非 auto 变量类型已在前面确定，默认为 null
  }
  return std::any(definedSymbols);
}

/**
 * @brief 访问 For-Each 循环控制 (ForEachExplicitControlContext)。
 * 处理 `declaration_item : expression`
 */
std::any SemanticAnalyzerVisitor::visitForEachExplicitControl(
    LangParser::ForEachExplicitControlContext *ctx) {
  PLOGV << "visitForEachExplicitControl (Trusting declared types): " << ctx->getText();

  // For-Each 循环变量的作用域是整个 for 语句（已在 visitForStmt 中创建）

  // 1. 分析集合表达式 (主要用于发现其内部错误，不用于推断循环变量类型)
  TypeInfoPtr collectionType = inferExpressionType(ctx->expression());
  if (!collectionType) { // inferExpressionType 可能返回 nullptr
    PLOGW << " ->无法推断 For-Each 集合表达式的类型: " << ctx->expression()->getText();
    // 即使集合表达式类型未知，仍然继续定义循环变量
  } else {
    PLOGV << " -> For-Each 集合表达式类型 (仅分析，不用于推断): " << collectionType->toString();
    // 可选：检查 collectionType 是否根本不可迭代 (例如 int), 但规范似乎允许运行时决定
    // if (!isIterable(collectionType)) { // isIterable 需要自行实现
    //     addDiagnostic(getRange(ctx->expression()), "类型 '" + collectionType->toString() + "'
    //     不是可迭代的", DiagnosticSeverity::Error);
    // }
  }

  // 2. 获取并处理循环变量声明列表
  auto declItems = ctx->declaration_item();
  size_t numLoopVars = declItems.size();
  PLOGV << " -> 声明了 " << numLoopVars << " 个循环变量";

  for (size_t i = 0; i < numLoopVars; ++i) {
    LangParser::Declaration_itemContext *declItemCtx = declItems[i];
    if (!declItemCtx || !declItemCtx->IDENTIFIER()) {
      PLOGE << " -> 错误: 无效的 declaration_item at index " << i;
      continue; // 跳过无效的声明项
    }

    std::string loopVarName = declItemCtx->IDENTIFIER()->getText();
    Location defLoc = getLocation(declItemCtx->IDENTIFIER());
    TypeInfoPtr loopVarDeclaredType = UnknownType; // 循环变量最终确定的类型
    bool isAuto = false;
    antlr4::Token *autoToken = nullptr;

    // 3. 解析声明的类型或 auto
    if (declItemCtx->type()) {
      loopVarDeclaredType = resolveTypeFromContext(declItemCtx->type());
      if (!loopVarDeclaredType || loopVarDeclaredType == UnknownType) {
        addDiagnostic(getRange(declItemCtx->type()),
                      "循环变量 '" + loopVarName + "' 的类型注解无效", DiagnosticSeverity::Error);
        loopVarDeclaredType = UnknownType; // 标记为错误
      } else if (loopVarDeclaredType->isEqualTo(*VoidType)) {
        addDiagnostic(getRange(declItemCtx->type()),
                      "不能声明 void 类型的循环变量 '" + loopVarName + "'",
                      DiagnosticSeverity::Error);
        loopVarDeclaredType = UnknownType; // 标记为错误
      }
      PLOGV << "  -> 循环变量 '" << loopVarName << "' 显式声明类型: "
            << (loopVarDeclaredType ? loopVarDeclaredType->toString() : "<unknown>");
    } else if (declItemCtx->AUTO()) {
      isAuto = true;
      autoToken = declItemCtx->AUTO()->getSymbol();
      // 根据规范，auto 在 for-each 中应视为 any，因为无法静态确定迭代值类型
      loopVarDeclaredType = AnyType;
      PLOGW << " -> For-Each 循环变量 '" << loopVarName << "' 使用 'auto'，按规范视为 'any' 类型。";
      // 可以选择性地添加一个警告或提示
      addDiagnostic(getTokenRange(autoToken),
                    "在 for-each 循环中使用 'auto' 声明变量 '" + loopVarName +
                        "'，其类型将视为 'any'。",
                    DiagnosticSeverity::Hint);

    } else {
      // 语法上应该不可能到这里，但还是处理一下
      addDiagnostic(getRange(declItemCtx), "循环变量 '" + loopVarName + "' 缺少类型注解或 'auto'",
                    DiagnosticSeverity::Error);
      loopVarDeclaredType = UnknownType;
    }

    // 如果解析类型失败，默认为 Unknown
    if (!loopVarDeclaredType) {
      loopVarDeclaredType = UnknownType;
    }

    // 4. 定义循环变量符号 (使用开发者指定的类型或 auto 推断的 any)
    SymbolInfoPtr loopVarSymbol = std::make_shared<SymbolInfo>(
        loopVarName, SymbolKind::VARIABLE, loopVarDeclaredType, defLoc,
        symbolTable.getCurrentScope(), declItemCtx); // 传入 declItemCtx
    loopVarSymbol->isConst = false;                  // 循环变量通常不是常量
    loopVarSymbol->isGlobal = false;

    if (!symbolTable.defineSymbol(loopVarSymbol)) {
      // 处理重定义错误
      SymbolInfoPtr existingSymbol = symbolTable.getCurrentScope()->resolveLocally(loopVarName);
      std::string errorMsg = "循环变量 '" + loopVarName + "' 在作用域中重复定义";
      if (existingSymbol) {
        errorMsg += "。先前定义在 line " +
                    std::to_string(existingSymbol->definitionLocation.range.start.line + 1);
      }
      addDiagnostic(getRange(declItemCtx->IDENTIFIER()), errorMsg, DiagnosticSeverity::Error);
    } else {
      analysisResult.nodeSymbolMap[declItemCtx->IDENTIFIER()] = loopVarSymbol; // 映射标识符节点
      PLOGD << "成功定义 For-Each 循环变量 '" << loopVarName
            << "' 类型: " << loopVarDeclaredType->toString();
    }

    // 5. **移除** 原来的类型检查逻辑
    //    不再需要比较集合元素类型和循环变量声明类型
    // if (!isAuto && loopVarDeclaredType != UnknownType && elementType != UnknownType) {
    //     if (!checkTypeAssignable(elementType, loopVarDeclaredType, ctx->declaration_item(i))) {
    //     ... }
    // }
  }

  return std::any();
}

/**
 * @brief 访问 For 循环更新部分 (ForUpdateContext)。
 */
std::any SemanticAnalyzerVisitor::visitForUpdate(LangParser::ForUpdateContext *ctx) {
  PLOGV << "visitForUpdate";
  // 访问所有单个更新语句/表达式
  for (auto updateSingleCtx : ctx->forUpdateSingle()) {
    visit(updateSingleCtx);
  }
  return std::any();
}

/**
 * @brief 访问单个 For 更新项 (ForUpdateSingleContext)。
 */
std::any SemanticAnalyzerVisitor::visitForUpdateSingle(LangParser::ForUpdateSingleContext *ctx) {
  PLOGV << "visitForUpdateSingle";
  // 可能是 expression, updateStatement, 或 assignStatement
  if (ctx->expression()) {
    inferExpressionType(ctx->expression()); // 访问表达式以进行检查
  } else if (ctx->updateStatement()) {
    visit(ctx->updateStatement()); // 访问更新赋值语句
  } else if (ctx->assignStatement()) {
    visit(ctx->assignStatement()); // 访问普通赋值语句
  }
  return std::any();
}

// --- 列表/映射字面量 --- (Primary 表达式的一部分)

/**
 * @brief 访问列表字面量 (PrimaryListLiteralContext)。
 */
std::any
SemanticAnalyzerVisitor::visitPrimaryListLiteral(LangParser::PrimaryListLiteralContext *ctx) {
  PLOGV << "visitPrimaryListLiteral";
  return visit(ctx->listExpression()); // 访问具体的 listExpression
}

/**
 * @brief 访问列表表达式定义 (ListLiteralDefContext)。
 * 处理 `[ [expressionList] ]`
 * @return std::any 包含推断出的 list 类型 TypeInfoPtr。
 */
std::any SemanticAnalyzerVisitor::visitListLiteralDef(LangParser::ListLiteralDefContext *ctx) {
  PLOGV << "visitListLiteralDef";
  TypeInfoPtr elementType = UnknownType; // 用于推断通用元素类型

  if (ctx->expressionList()) {
    // 访问表达式列表以获取所有元素的类型
    std::vector<TypeInfoPtr> elementTypes;
    std::any result = visit(ctx->expressionList());
    try {
      elementTypes = std::any_cast<std::vector<TypeInfoPtr>>(result);
    } catch (...) { /* 错误处理 */
    }

    if (!elementTypes.empty()) {
      // 推断通用类型：
      // - 如果所有元素类型相同（或可隐式转换到某个共同类型），则为 list<CommonType>
      // - 如果包含不同类型（或 any/unknown），则为 list<any>
      // 简化逻辑：如果所有已知类型都相同，则使用该类型，否则使用 any
      TypeInfoPtr firstKnownType = UnknownType;
      bool allSame = true;
      for (const auto &type : elementTypes) {
        if (type != UnknownType) {
          if (firstKnownType == UnknownType) {
            firstKnownType = type;
          } else if (!firstKnownType->isEqualTo(*type)) {
            allSame = false;
            break;
          }
        } else {
          // 如果存在未知类型，无法确定共同类型，只能是 any
          allSame = false;
          break;
        }
      }

      if (allSame && firstKnownType != UnknownType) {
        elementType = firstKnownType;
        PLOGV << " -> 列表字面量推断元素类型: " << elementType->toString();
      } else {
        elementType = AnyType; // 包含不同类型、未知类型或 any 类型
        PLOGV << " -> 列表字面量元素类型不统一或未知，推断为: any";
      }
    } else {
      // 空列表 []，元素类型无法确定，通常推断为 list<any>
      elementType = AnyType;
      PLOGV << " -> 空列表字面量，推断为: list<any>";
    }

  } else {
    // 空列表 [] -> list<any>
    elementType = AnyType;
    PLOGV << " -> 空列表字面量，推断为: list<any>";
  }
  TypeInfoPtr listType = std::make_shared<ListType>(elementType);
  return std::any(listType);
}

/**
 * @brief 访问映射字面量 (PrimaryMapLiteralContext)。
 */
std::any
SemanticAnalyzerVisitor::visitPrimaryMapLiteral(LangParser::PrimaryMapLiteralContext *ctx) {
  PLOGV << "visitPrimaryMapLiteral";
  return visit(ctx->mapExpression()); // 访问具体的 mapExpression
}

/**
 * @brief 访问映射表达式定义 (MapLiteralDefContext)。
 * 处理 `{ [mapEntryList] }`
 * @return std::any 包含推断出的 map 类型 TypeInfoPtr。
 */
std::any SemanticAnalyzerVisitor::visitMapLiteralDef(LangParser::MapLiteralDefContext *ctx) {
  PLOGV << "visitMapLiteralDef";
  TypeInfoPtr commonKeyType = UnknownType;
  TypeInfoPtr commonValueType = UnknownType;
  bool firstEntry = true;
  bool keyTypesConsistent = true;
  bool valueTypesConsistent = true;
  std::set<std::string> keys; // 用于检查重复键 (仅限字面量键)

  if (ctx->mapEntryList()) {
    for (auto entryCtx : ctx->mapEntryList()->mapEntry()) {
      // 访问每个 entry 以获取 key 和 value 的类型
      std::any entryResult = visit(entryCtx); // 期望返回 pair<keyType, valueType>
      TypeInfoPtr keyType = UnknownType;
      TypeInfoPtr valueType = UnknownType;
      std::string literalKey; // 用于存储字面量键以检查重复

      try {
        auto types = std::any_cast<std::pair<TypeInfoPtr, TypeInfoPtr>>(entryResult);
        keyType = types.first;
        valueType = types.second;

        // 尝试获取字面量键
        if (auto identKeyCtx = dynamic_cast<LangParser::MapEntryIdentKeyContext *>(entryCtx)) {
          literalKey = identKeyCtx->IDENTIFIER()->getText();
        } else if (auto stringKeyCtx =
                       dynamic_cast<LangParser::MapEntryStringKeyContext *>(entryCtx)) {
          // 去掉引号
          std::string rawKey = stringKeyCtx->STRING_LITERAL()->getText();
          if (rawKey.length() >= 2) {
            literalKey = rawKey.substr(1, rawKey.length() - 2);
            // TODO: 处理转义字符
          }
        }

      } catch (...) {
        addDiagnostic(getRange(entryCtx), "内部错误：处理 map entry 返回值失败",
                      DiagnosticSeverity::Error);
        keyTypesConsistent = false;
        valueTypesConsistent = false;
        continue; // 跳过此条目
      }

      // 检查重复键 (仅对字面量键)
      if (!literalKey.empty()) {
        if (keys.count(literalKey)) {
          addDiagnostic(getRange(entryCtx), "映射字面量中存在重复键 '" + literalKey + "'",
                        DiagnosticSeverity::Warning); // 通常是警告
        } else {
          keys.insert(literalKey);
        }
      } else {
        // 表达式键无法在编译时检查重复
        PLOGV << " -> 映射条目使用表达式作为键，跳过重复检查";
      }

      // 更新通用类型
      if (firstEntry) {
        commonKeyType = (keyType != UnknownType) ? keyType : AnyType;
        commonValueType = (valueType != UnknownType) ? valueType : AnyType;
        firstEntry = false;
      } else {
        // 如果当前键类型与通用键类型不兼容（且都不是 Unknown/Any），则通用键类型变为 Any
        if (keyTypesConsistent && commonKeyType != AnyType && keyType != UnknownType &&
            keyType != AnyType && !commonKeyType->isEqualTo(*keyType)) {
          commonKeyType = AnyType;
          keyTypesConsistent = false; // 标记为不一致
          PLOGV << " -> 映射键类型不一致，通用键类型变为 any";
        }
        if (commonKeyType == UnknownType && keyType != UnknownType)
          commonKeyType = keyType; // 更新第一个已知的键类型

        // 如果当前值类型与通用值类型不兼容（且都不是 Unknown/Any），则通用值类型变为 Any
        if (valueTypesConsistent && commonValueType != AnyType && valueType != UnknownType &&
            valueType != AnyType && !commonValueType->isEqualTo(*valueType)) {
          commonValueType = AnyType;
          valueTypesConsistent = false; // 标记为不一致
          PLOGV << " -> 映射值类型不一致，通用值类型变为 any";
        }
        if (commonValueType == UnknownType && valueType != UnknownType)
          commonValueType = valueType; // 更新第一个已知的值类型
      }
    }
  } else {
    // 空映射 {} -> map<any, any>
    commonKeyType = AnyType;
    commonValueType = AnyType;
    PLOGV << " -> 空映射字面量，推断为 map<any, any>";
  }

  // 如果在处理过程中遇到错误导致类型仍为 Unknown，则默认为 any
  if (commonKeyType == UnknownType)
    commonKeyType = AnyType;
  if (commonValueType == UnknownType)
    commonValueType = AnyType;

  PLOGV << " -> 映射字面量最终推断类型: map<" << commonKeyType->toString() << ", "
        << commonValueType->toString() << ">";
  TypeInfoPtr result = std::make_shared<MapType>(commonKeyType, commonValueType);
  return std::any(result);
}

/**
 * @brief 访问映射条目 (MapEntryContext 的不同子类)。
 * @return std::any 包含 std::pair<TypeInfoPtr, TypeInfoPtr> (键类型, 值类型)。
 */
std::any SemanticAnalyzerVisitor::visitMapEntryIdentKey(LangParser::MapEntryIdentKeyContext *ctx) {
  PLOGV << "visitMapEntryIdentKey: " << ctx->IDENTIFIER()->getText();
  // 标识符作为键，类型是 string
  TypeInfoPtr keyType = StringType;
  TypeInfoPtr valueType = inferExpressionType(ctx->expression());
  return std::any(std::make_pair(keyType, valueType));
}

std::any SemanticAnalyzerVisitor::visitMapEntryExprKey(LangParser::MapEntryExprKeyContext *ctx) {
  PLOGV << "visitMapEntryExprKey: [" << ctx->expression(0)->getText() << "]";
  // 表达式作为键
  TypeInfoPtr keyType = inferExpressionType(ctx->expression(0));
  // 检查键类型是否为 null
  if (keyType->isEqualTo(*NullType)) {
    addDiagnostic(getRange(ctx->expression(0)), "映射的键不能是 'null'", DiagnosticSeverity::Error);
    keyType = UnknownType; // 标记键类型无效
  }
  TypeInfoPtr valueType = inferExpressionType(ctx->expression(1));
  return std::any(std::make_pair(keyType, valueType));
}

std::any
SemanticAnalyzerVisitor::visitMapEntryStringKey(LangParser::MapEntryStringKeyContext *ctx) {
  PLOGV << "visitMapEntryStringKey: " << ctx->STRING_LITERAL()->getText();
  // 字符串字面量作为键，类型是 string
  TypeInfoPtr keyType = StringType;
  TypeInfoPtr valueType = inferExpressionType(ctx->expression());
  return std::any(std::make_pair(keyType, valueType));
}

// --- New 表达式 ---

/**
 * @brief 访问 new 表达式的 Primary 形式 (PrimaryNewContext)。
 */
std::any SemanticAnalyzerVisitor::visitPrimaryNew(LangParser::PrimaryNewContext *ctx) {
  PLOGV << "visitPrimaryNew";
  return visit(ctx->newExp());
}

/**
 * @brief 访问 new 表达式定义 (NewExpressionDefContext)。
 * 处理 `new qualifiedIdentifier ( [arguments] )`
 * @return std::any 包含创建的类实例的 TypeInfoPtr (即 ClassType)。
 */
std::any SemanticAnalyzerVisitor::visitNewExpressionDef(LangParser::NewExpressionDefContext *ctx) {
  std::string className;
  try {
    className = std::any_cast<std::string>(visit(ctx->qualifiedIdentifier()));
  } catch (...) {
    addDiagnostic(getRange(ctx->qualifiedIdentifier()), "内部错误：无法获取 new 表达式的类名",
                  DiagnosticSeverity::Error);
    return std::any(UnknownType);
  }
  PLOGV << "visitNewExpressionDef: new " << className;

  // 1. 查找类符号
  SymbolInfoPtr classSymbol = symbolTable.resolveSymbol(className);
  if (!classSymbol || classSymbol->kind != SymbolKind::CLASS) {
    addDiagnostic(getRange(ctx->qualifiedIdentifier()), "未找到或无效的类名 '" + className + "'",
                  DiagnosticSeverity::Error);
    return std::any(UnknownType);
  }

  // 2. 获取类类型信息 (ClassType)
  auto classType = std::dynamic_pointer_cast<ClassType>(classSymbol->type);
  if (!classType) {
    addDiagnostic(getRange(ctx->qualifiedIdentifier()),
                  "内部错误：类符号 '" + className + "' 没有关联的 ClassType 信息",
                  DiagnosticSeverity::Error);
    return std::any(UnknownType);
  }

  // 3. 查找构造函数 (__init__)
  SymbolInfoPtr constructorSymbol = nullptr;
  if (classType->classScope) {
    constructorSymbol = classType->classScope->resolveLocally("__init__");
  }

  TypeInfoPtr constructorType = nullptr;
  std::shared_ptr<FunctionSignature> constructorSig = nullptr;
  if (constructorSymbol) {
    constructorType = constructorSymbol->type;
    constructorSig = std::dynamic_pointer_cast<FunctionSignature>(constructorType);
    // 构造函数签名必须是 void __init__(...)
    if (!constructorSig || !constructorSymbol->isStatic /* 构造函数不是静态的 */) {
      addDiagnostic(constructorSymbol->definitionLocation.range,
                    "类 '" + className + "' 的构造函数 '__init__' 签名无效",
                    DiagnosticSeverity::Error);
      constructorSig = nullptr; // 标记构造函数无效
    }
    // 检查返回类型是否为 void
    if (constructorSig && std::holds_alternative<TypeInfoPtr>(constructorSig->returnTypeInfo)) {
      auto retType = std::get<TypeInfoPtr>(constructorSig->returnTypeInfo);
      if (!retType || !retType->isEqualTo(*VoidType)) {
        addDiagnostic(constructorSymbol->definitionLocation.range,
                      "构造函数 '__init__' 的返回类型必须是 void", DiagnosticSeverity::Error);
        // 可以继续检查参数，但构造函数签名已视为无效
      }
    } else if (constructorSig && std::holds_alternative<FunctionSignature::MultiReturnTag>(
                                     constructorSig->returnTypeInfo)) {
      addDiagnostic(constructorSymbol->definitionLocation.range,
                    "构造函数 '__init__' 不能声明为多返回值 (mutivar)", DiagnosticSeverity::Error);
    }
  }
  // 如果没有找到 __init__，则假定存在一个默认的无参构造函数

  // 4. 处理构造函数参数 (类似函数调用)
  std::vector<TypeInfoPtr> argTypes;
  if (ctx->arguments()) { // 检查是否有参数列表 '()'
    // 即使是 new MyClass()，也会有 arguments 节点，但可能没有 expressionList
    if (ctx->arguments()->expressionList()) {
      std::any argListResult = visit(ctx->arguments()->expressionList());
      try {
        argTypes = std::any_cast<std::vector<TypeInfoPtr>>(argListResult);
      } catch (...) { /* 错误处理 */
      }
    }
  }
  PLOGV << " -> 调用构造函数，参数数量: " << argTypes.size();

  // 5. 检查构造函数参数
  if (constructorSig) {
    size_t requiredParams = constructorSig->parameters.size();
    size_t providedArgs = argTypes.size();
    bool argCountOk = true;
    if (constructorSig->isVariadic) {
      if (providedArgs < requiredParams)
        argCountOk = false;
    } else {
      if (providedArgs != requiredParams)
        argCountOk = false;
    }
    if (!argCountOk) {
      std::string expected = constructorSig->isVariadic ? "至少 " + std::to_string(requiredParams)
                                                        : std::to_string(requiredParams);
      // *** 简化错误定位到整个 new 表达式上下文 ***
      addDiagnostic(getRange(ctx),
                    "构造函数调用参数数量不匹配，期望 " + expected + " 个，实际提供 " +
                        std::to_string(providedArgs),
                    DiagnosticSeverity::Error);
    } else { /* TODO ... 参数类型检查逻辑 ... */
    }
  } else {
    if (!argTypes.empty()) {
      // *** 修正：简化错误定位到整个 new 表达式上下文 ***
      addDiagnostic(getRange(ctx), "类 '" + className + "' 没有显式构造函数，但调用时提供了参数",
                    DiagnosticSeverity::Error);
    }
  }
  TypeInfoPtr resultType = classType;
  // 6. new 表达式的结果类型是类本身
  PLOGV << " -> new 表达式结果类型: " << classType->toString();
  return std::any(resultType);
}

// --- Lambda 表达式 ---

/**
 * @brief 访问 Lambda 表达式的 Primary 形式 (PrimaryLambdaContext)。
 */
std::any SemanticAnalyzerVisitor::visitPrimaryLambda(LangParser::PrimaryLambdaContext *ctx) {
  PLOGV << "visitPrimaryLambda";
  return visit(ctx->lambdaExpression());
}

/**
 * @brief 访问 Lambda 表达式定义 (LambdaExprDefContext)。
 * 处理 `function ( [paramList] ) -> (type|mutivar) blockStatement`
 * @return std::any 包含 Lambda 的 FunctionSignature 类型 TypeInfoPtr。
 */
std::any SemanticAnalyzerVisitor::visitLambdaExprDef(LangParser::LambdaExprDefContext *ctx) {
  PLOGD << "visitLambdaExprDef";
  // Lambda 表达式本身是一个值，其类型是 FunctionSignature

  // 1. 创建函数签名
  auto signature = std::make_shared<FunctionSignature>();
  bool isMultiReturn = (ctx->MUTIVAR() != nullptr);

  // 2. 解析返回类型
  if (isMultiReturn) {
    signature->returnTypeInfo = FunctionSignature::MultiReturnTag{};
    PLOGV << " -> Lambda 返回类型: mutivar";
  } else if (ctx->type()) {
    TypeInfoPtr returnType = resolveTypeFromContext(ctx->type());
    if (!returnType || returnType == UnknownType) {
      addDiagnostic(getRange(ctx->type()), "Lambda 返回类型无效或未知",
                    DiagnosticSeverity::Warning);
      returnType = UnknownType;
    }
    signature->returnTypeInfo = returnType;
    PLOGV << " -> Lambda 返回类型: " << returnType->toString();
  } else {
    // 语法应该保证 -> 后面有 type 或 mutivar
    addDiagnostic(getRange(ctx->ARROW()), "Lambda 表达式缺少返回类型 (type 或 mutivar)",
                  DiagnosticSeverity::Error);
    signature->returnTypeInfo = UnknownType; // 标记错误
  }

  // 3. 创建 Lambda 的作用域 (非常重要，用于捕获和参数定义)
  ScopeGuard lambdaScopeGuard(symbolTable, ScopeKind::FUNCTION, ctx); // Lambda 类似函数作用域
  // 记录作用域范围
  recordScopeRange(symbolTable.getCurrentScope(), ctx);
  // 4. 压入预期返回类型
  if (isMultiReturn)
    expectedReturnTypeStack.push(nullptr);
  else
    expectedReturnTypeStack.push(std::get<TypeInfoPtr>(signature->returnTypeInfo));

  // 5. 处理参数列表 (在 Lambda 作用域内定义)
  bool isVariadic = false;
  if (ctx->parameterList()) {
    std::any paramResult = visit(ctx->parameterList());
    try {
      auto paramsPair = std::any_cast<std::pair<std::vector<SymbolInfoPtr>, bool>>(paramResult);
      isVariadic = paramsPair.second;
      for (const auto &paramSymbol : paramsPair.first) {
        if (paramSymbol && paramSymbol->type) {
          signature->parameters.push_back({paramSymbol->name, paramSymbol->type});
        }
      }
    } catch (...) { /* 错误处理 */
    }
  }
  signature->isVariadic = isVariadic;

  // 6. 访问 Lambda 体 (BlockStatement)
  // 此时内部的 return 语句会检查 expectedReturnTypeStack
  visit(ctx->blockStatement());

  // 7. 弹出预期返回类型
  if (!expectedReturnTypeStack.empty()) {
    expectedReturnTypeStack.pop();
  } else {
    PLOGE << "内部错误：尝试弹出空的预期返回类型栈 (Lambda)";
  }

  // ScopeGuard 会自动弹出 Lambda 作用域

  PLOGV << " -> Lambda 表达式类型: " << signature->toString();
  TypeInfoPtr result = signature;
  return std::any(result);
}

// --- 可变参数与导入语句 ---

/**
 * @brief 访问可变参数 '...' (PrimaryVarArgsContext)。
 * @return std::any 包含代表可变参数的类型 (例如 list<any> 或特殊类型)。
 */
std::any SemanticAnalyzerVisitor::visitPrimaryVarArgs(LangParser::PrimaryVarArgsContext *ctx) {
  PLOGV << "visitPrimaryVarArgs";
  // TODO: 检查 '...' 是否在可变参数函数内部
  // 需要访问函数签名的 isVariadic 标志
  // if (!currentFunctionSignature || !currentFunctionSignature->isVariadic) {
  //      addDiagnostic(getRange(ctx), "'...' 只能用在可变参数函数内部");
  //      return std::any(UnknownType);
  // }

  // 可变参数在 SptScript 中如何表示？通常是 list<any>
  PLOGV << " -> 可变参数 '...' 类型假定为 list<any>";
  TypeInfoPtr result = std::make_shared<ListType>(AnyType);
  return std::any(result);
}

/**
 * @brief 访问 Import 语句的顶层节点 (ImportStmtContext)。
 */
std::any SemanticAnalyzerVisitor::visitImportStmt(LangParser::ImportStmtContext *ctx) {
  PLOGD << "visitImportStmt";
  // 访问具体的导入语句子类型 (Namespace or Named)
  return visitChildren(ctx);
}

/**
 * @brief 访问命名空间导入 (ImportNamespaceStmtContext)。
 * 处理 `import IDENTIFIER from STRING_LITERAL`
 */
std::any
SemanticAnalyzerVisitor::visitImportNamespaceStmt(LangParser::ImportNamespaceStmtContext *ctx) {
  std::string namespaceName = ctx->IDENTIFIER()->getText();
  std::string modulePath = ctx->STRING_LITERAL()->getText();
  // 去掉字符串字面量的引号
  if (modulePath.length() >= 2) {
    modulePath = modulePath.substr(1, modulePath.length() - 2);
  } else {
    addDiagnostic(getRange(ctx->STRING_LITERAL()), "无效的模块路径字符串",
                  DiagnosticSeverity::Error);
    return std::any();
  }
  PLOGD << "visitImportNamespaceStmt: import " << namespaceName << " from \"" << modulePath << "\"";

  // 1. 处理导入，获取导入模块的作用域
  std::shared_ptr<Scope> importedScope = processImport(modulePath, ctx);
  if (!importedScope) {
    // processImport 内部会报告错误
    return std::any();
  }

  // 2. 创建一个代表导入模块的符号 (类型可以是一个特殊的 ModuleType 或 MapType)
  // 这里简化为 AnyType，因为静态跟踪模块导出比较复杂
  TypeInfoPtr moduleType = AnyType; // TODO: 考虑创建 ModuleType 或 MapType 来表示导出的成员
  Location defLoc = getLocation(ctx->IDENTIFIER());
  SymbolInfoPtr namespaceSymbol = std::make_shared<SymbolInfo>(
      namespaceName, SymbolKind::MODULE, moduleType, defLoc, symbolTable.getCurrentScope(), ctx);
  // 关联导入的作用域，以便后续查找成员？ (需要 SymbolInfo 支持)
  // namespaceSymbol->importedScope = importedScope;

  // 3. 在当前作用域定义命名空间符号
  if (!symbolTable.defineSymbol(namespaceSymbol)) {
    addDiagnostic(getRange(ctx->IDENTIFIER()),
                  "导入的命名空间名称 '" + namespaceName + "' 与现有符号冲突",
                  DiagnosticSeverity::Error);
  } else {
    PLOGD << "成功定义导入的命名空间符号 '" << namespaceName << "'";
  }

  return std::any();
}

/**
 * @brief 访问命名导入 (ImportNamedStmtContext)。
 * 处理 `import { id1, id2, ... } from STRING_LITERAL`
 */
std::any SemanticAnalyzerVisitor::visitImportNamedStmt(LangParser::ImportNamedStmtContext *ctx) {
  if (!ctx->STRING_LITERAL()) {
    addDiagnostic(getRange(ctx), "导入名称不能为空", DiagnosticSeverity::Error);
    return std::any();
  }
  std::string modulePath = ctx->STRING_LITERAL()->getText();
  // 去掉引号
  if (modulePath.length() >= 2) {
    modulePath = modulePath.substr(1, modulePath.length() - 2);
  } else {
    addDiagnostic(getRange(ctx->STRING_LITERAL()), "无效的模块路径字符串",
                  DiagnosticSeverity::Error);
    return std::any();
  }
  PLOGD << "visitImportNamedStmt: from \"" << modulePath << "\"";

  // 1. 处理导入，获取导入模块的作用域
  std::shared_ptr<Scope> importedScope = processImport(modulePath, ctx);
  if (!importedScope) {
    return std::any();
  }

  // 2. 遍历每个 importSpecifier
  for (auto specCtx : ctx->importSpecifier()) {
    std::string importName;
    // visitImportSpecifier 应该返回导入的名称字符串
    std::any nameResult = visit(specCtx);
    try {
      importName = std::any_cast<std::string>(nameResult);
    } catch (...) {
      addDiagnostic(getRange(specCtx), "内部错误：无法获取导入名称", DiagnosticSeverity::Error);
      continue;
    }
    PLOGV << " -> 尝试导入名称: " << importName;

    // 3. 在导入模块的作用域中查找该名称
    SymbolInfoPtr importedSymbol = importedScope->resolveLocally(importName); // 只在顶层查找

    if (!importedSymbol) {
      addDiagnostic(getRange(specCtx),
                    "名称 '" + importName + "' 未在模块 '" + modulePath + "' 中导出或未找到",
                    DiagnosticSeverity::Error);
      continue;
    }
    // 规范没有明确 export 必须存在，但通常导入的都应该是导出的
    if (!importedSymbol->isExported) {
      addDiagnostic(getRange(specCtx),
                    "尝试导入模块 '" + modulePath + "' 中未导出的名称 '" + importName + "'",
                    DiagnosticSeverity::Warning);
      // 可以选择继续导入，或者报错
    }

    // 4. 在当前作用域定义一个新的符号，复制类型和种类等信息
    Location defLoc = getLocation(specCtx); // 定义位置是 import specifier 的位置
    SymbolInfoPtr localSymbol =
        std::make_shared<SymbolInfo>(importName,                         // 使用相同的名称
                                     importedSymbol->kind,               // 复制种类
                                     importedSymbol->type,               // 复制类型
                                     importedSymbol->definitionLocation, // 定义位置是 import 处
                                     symbolTable.getCurrentScope(),      // 作用域是当前作用域
                                     specCtx);
    localSymbol->isConst = importedSymbol->isConst; // 复制 constness
    // isGlobal, isStatic 通常不适用于导入的别名
    // 可以添加一个标志表示这是导入的符号
    // localSymbol->isImported = true;

    if (!symbolTable.defineSymbol(localSymbol)) {
      addDiagnostic(getRange(specCtx), "导入的名称 '" + importName + "' 与当前作用域的现有符号冲突",
                    DiagnosticSeverity::Error);
    } else {
      PLOGD << "成功导入并定义符号 '" << importName
            << "' 类型: " << (localSymbol->type ? localSymbol->type->toString() : "<null>");
    }
  }

  return std::any();
}

/**
 * @brief 访问导入说明符 (ImportSpecifierContext)。
 * 处理 `IDENTIFIER`
 * @return std::any 包含导入的标识符名称字符串。
 */
std::any SemanticAnalyzerVisitor::visitImportSpecifier(LangParser::ImportSpecifierContext *ctx) {
  auto identifiers = ctx->IDENTIFIER();
  if (identifiers.empty()) {
    PLOGE << "内部错误: ImportSpecifier 缺少 IDENTIFIER";
    addDiagnostic(getRange(ctx), "内部错误: 导入项缺少名称", DiagnosticSeverity::Error);
    return std::any(std::string("")); // 返回空字符串表示错误
  }
  // 第一个标识符始终是原始导入名称或要定义的本地名称
  std::string name = identifiers[0]->getText();
  // 如果有 'as' 关键字和第二个标识符，则第一个是原始名，第二个是别名
  // visitImportNamedStmt 需要的是在当前作用域定义的名称
  // 如果有 'as', 那么定义的名称是第二个; 否则是第一个
  if (ctx->AS() && identifiers.size() > 1) {
    name = identifiers[1]->getText(); // 使用别名作为要在本地定义的名字
  }
  PLOGV << "visitImportSpecifier: processing specifier for local name '" << name << "'";
  return std::any(name); // 返回要在本地作用域使用的名称
}

// --- Mutivar 赋值语句 ---

/**
 * @brief 访问函数调用参数 (ArgumentsContext)。(需要头文件声明+override)
 * 处理 `( [expressionList] )` 中的 expressionList。
 */
std::any SemanticAnalyzerVisitor::visitArguments(LangParser::ArgumentsContext *ctx) {
  PLOGV << "visitArguments: " << ctx->getText();
  if (ctx->expressionList()) {
    // 如果存在表达式列表，则访问它
    // visitExpressionList 会返回 std::any 包含 std::vector<TypeInfoPtr>
    return visit(ctx->expressionList());
  }
  // 如果没有 expressionList，表示空参数列表 ()
  // 返回一个空的类型向量
  return std::any(std::vector<TypeInfoPtr>());
}

/**
 * @brief 访问表达式列表 (ExpressionListContext)。(需要头文件声明+override)
 * 处理 `expression (, expression)*`。
 * @return std::any 包含 std::vector<TypeInfoPtr>，存储每个表达式的类型。
 */
std::any SemanticAnalyzerVisitor::visitExpressionList(LangParser::ExpressionListContext *ctx) {
  PLOGV << "visitExpressionList: " << ctx->getText();
  std::vector<TypeInfoPtr> types;
  for (auto exprCtx : ctx->expression()) {
    // 推断每个表达式的类型并添加到列表中
    TypeInfoPtr exprType = inferExpressionType(exprCtx);
    types.push_back(exprType ? exprType : UnknownType); // 如果推断失败，添加 UnknownType
  }
  PLOGV << " -> 表达式列表包含 " << types.size() << " 个表达式";
  return std::any(types); // 返回包含所有参数类型的向量
}

// --- 结束 Part 5 ---
// src/semantic_analyzer_visitor.cpp

// --- 在合适的位置添加或修改以下函数 ---

/**
 * @brief 访问表达式语句 (ExpressionStmtContext)。(补充可选警告)
 */
std::any SemanticAnalyzerVisitor::visitExpressionStmt(LangParser::ExpressionStmtContext *ctx) {
  PLOGV << "visitExpressionStmt: " << ctx->getText();
  std::any exprResult = visit(ctx->expression()); // 访问子表达式

  // 可选：检查未使用的结果
  try {
    TypeInfoPtr exprType = std::any_cast<TypeInfoPtr>(exprResult);
    // 如果表达式有类型（不是 Unknown 或 void），并且父节点不是赋值、返回等会使用其值的语句，
    // 则可以发出警告。这里简化处理：如果不是 void，就可能未使用。
    if (exprType && !exprType->isEqualTo(*VoidType) && !exprType->isEqualTo(*UnknownType)) {
      // 更复杂的检查可以看父节点类型 ctx->parent->getRuleIndex() 等
      // 暂时不加警告，避免误报
      // addDiagnostic(getRange(ctx), "表达式结果未使用", DiagnosticSeverity::Hint);
    }
  } catch (...) { /* 忽略类型转换失败 */
  }

  return std::any(); // 表达式语句本身不返回值
}

/**
 * @brief 访问顶层表达式规则 (ExpressionContext)。
 * 通常直接委托给优先级链中的下一个表达式。
 */
std::any SemanticAnalyzerVisitor::visitExpression(LangParser::ExpressionContext *ctx) {
  PLOGV << "visitExpression: " << ctx->getText();
  // 根据语法 expression : logicalOrExp ;
  if (ctx->logicalOrExp()) {
    return visit(ctx->logicalOrExp());
  }
  // 如果语法允许 expression 有其他形式，需要在这里处理
  PLOGW << "访问了未预期结构的 ExpressionContext";
  return std::any(UnknownType); // 或者根据情况返回
}

/**
 * @brief 访问多返回值类方法成员 (MultiReturnClassMethodMemberContext)。
 * 处理 `[static?] mutivar IDENTIFIER ( [paramList] ) blockStatement`
 */
std::any SemanticAnalyzerVisitor::visitMultiReturnClassMethodMember(
    LangParser::MultiReturnClassMethodMemberContext *ctx) {
  bool isStatic = (ctx->STATIC() != nullptr);
  antlr4::tree::TerminalNode *identifierNode = ctx->IDENTIFIER(); // 获取名字节点
  std::string methodName = identifierNode->getText();
  PLOGD << "visitMultiReturnClassMethodMember: " << methodName << (isStatic ? " (Static)" : "");

  // 检查是否在类作用域内
  if (!currentClassType || symbolTable.getCurrentScope()->kind != ScopeKind::CLASS) {
    addDiagnostic(getRange(ctx), "多返回值方法声明只能出现在类定义内部", DiagnosticSeverity::Error);
    return std::any();
  }

  Location defLoc = getLocation(ctx->IDENTIFIER());

  // 1. 创建方法签名 (返回类型为 MultiReturnTag)
  auto signature = std::make_shared<FunctionSignature>();
  signature->returnTypeInfo = FunctionSignature::MultiReturnTag{};

  // 2. 创建并定义方法符号
  SymbolInfoPtr methodSymbol = std::make_shared<SymbolInfo>(
      methodName, SymbolKind::FUNCTION, signature, defLoc, symbolTable.getCurrentScope(), ctx);
  methodSymbol->isStatic = isStatic;

  if (!symbolTable.defineSymbol(methodSymbol)) {
    // 处理方法重定义错误
    SymbolInfoPtr existingSymbol = symbolTable.getCurrentScope()->resolveLocally(methodName);
    std::string errorMsg = (isStatic ? "静态" : "实例");
    errorMsg +=
        "多返回值方法 '" + methodName + "' 在类 '" + currentClassType->name + "' 中重复定义";
    if (existingSymbol) {
      errorMsg += "。先前定义在 line " +
                  std::to_string(existingSymbol->definitionLocation.range.start.line + 1);
    }
    addDiagnostic(getRange(ctx->IDENTIFIER()), errorMsg, DiagnosticSeverity::Error);
  } else {
    PLOGD << "成功定义多返回值方法符号 '" << methodName << "'" << (isStatic ? " (Static)" : "");
    analysisResult.nodeSymbolMap[identifierNode] = methodSymbol; // 映射方法名节点
  }

  // 3. 创建方法作用域
  ScopeGuard methodScopeGuard(symbolTable, ScopeKind::FUNCTION, ctx);
  recordScopeRange(symbolTable.getCurrentScope(), ctx);
  // 4. 处理 'this' 和上下文
  bool previousInStatic = isInStaticMethod;
  isInStaticMethod = isStatic;

  // 5. 压入预期返回类型 (nullptr 代表 mutivar)
  expectedReturnTypeStack.push(nullptr);
  PLOGV << "压入预期返回类型: mutivar (用 nullptr 表示)";

  // 6. 处理参数列表
  bool isVariadic = false;
  if (ctx->parameterList()) {
    std::any paramResult = visit(ctx->parameterList());
    try {
      auto paramsPair = std::any_cast<std::pair<std::vector<SymbolInfoPtr>, bool>>(paramResult);
      isVariadic = paramsPair.second;
      for (const auto &paramSymbol : paramsPair.first) {
        if (paramSymbol && paramSymbol->type) {
          signature->parameters.push_back({paramSymbol->name, paramSymbol->type});
        }
      }
    } catch (...) { /* 错误处理 */
    }
  }
  signature->isVariadic = isVariadic;

  // 7. 访问方法体
  visit(ctx->blockStatement());

  // 8. 弹出预期返回类型
  if (!expectedReturnTypeStack.empty()) {
    PLOGV << "弹出预期返回类型 (mutivar)";
    expectedReturnTypeStack.pop();
  } else {
    PLOGE << "内部错误：尝试弹出空的预期返回类型栈 (mutivar 方法)";
  }

  // 9. 恢复静态方法上下文
  isInStaticMethod = previousInStatic;

  // ScopeGuard 会自动弹出作用域

  return std::any(methodSymbol);
}

std::any SemanticAnalyzerVisitor::visitForStatement(LangParser::ForStatementContext *ctx) {
  return visitChildren(ctx);
}

// 在 semantic_analyzer_visitor.cpp 中
std::any SemanticAnalyzerVisitor::visitTypeQualifiedIdentifier(
    LangParser::TypeQualifiedIdentifierContext *ctx) {
  std::string typeName;
  try {
    // visit(ctx->qualifiedIdentifier()) 应该返回 std::string
    typeName = std::any_cast<std::string>(visit(ctx->qualifiedIdentifier()));
    PLOGD << "[VisitTypeQualifiedIdentifier] 类型名称: " << typeName;
  } catch (const std::bad_any_cast &e) {
    PLOGE << "[VisitTypeQualifiedIdentifier] 无法从 qualifiedIdentifier 获取名称: "
          << ctx->getText() << " - " << e.what();
    addDiagnostic(getRange(ctx), "内部错误: 解析类型限定名失败", DiagnosticSeverity::Error);
    return std::any(UnknownType); // 返回 TypeInfoPtr 类型的 UnknownType
  }

  SymbolInfoPtr symbol = symbolTable.resolveSymbol(typeName); // 在当前作用域链中查找
  if (symbol) {
    PLOGD << "[VisitTypeQualifiedIdentifier] -> 找到符号 '" << typeName
          << "'，Kind: " << static_cast<int>(symbol->kind);
    // 检查符号是否确实是一个类型 (Class, TypeAlias等)
    if (symbol->kind == SymbolKind::CLASS /* || symbol->kind == SymbolKind::TYPE_ALIAS 等 */) {
      if (symbol->type) { // 确保符号本身有关联的 TypeInfo
        PLOGD << "[VisitTypeQualifiedIdentifier] -> 返回符号的类型: " << symbol->type->toString();
        return std::any(symbol->type); // <--- 关键：返回 TypeInfoPtr
      } else {
        PLOGE << "[VisitTypeQualifiedIdentifier] -> 符号 '" << typeName << "' 的类型信息为空。";
        addDiagnostic(getRange(ctx), "类型 '" + typeName + "' 的定义不完整 (符号缺少类型)。",
                      DiagnosticSeverity::Error);
        return std::any(UnknownType); // 返回 TypeInfoPtr 类型的 UnknownType
      }
    } else {
      PLOGW << "[VisitTypeQualifiedIdentifier] -> 符号 '" << typeName
            << "' 不是一个有效的类型 kind。Kind: " << static_cast<int>(symbol->kind);
      addDiagnostic(getRange(ctx), "'" + typeName + "' 不是一个有效的类型名称。",
                    DiagnosticSeverity::Error);
      return std::any(UnknownType); // 返回 TypeInfoPtr 类型的 UnknownType
    }
  } else {
    PLOGW << "[VisitTypeQualifiedIdentifier] -> 未找到名为 '" << typeName << "' 的类型符号。";
    addDiagnostic(getRange(ctx), "未定义的类型名称 '" + typeName + "'。",
                  DiagnosticSeverity::Error);
    return std::any(UnknownType); // 返回 TypeInfoPtr 类型的 UnknownType
  }
}

#include <queue> // 需要包含 queue 头文件用于下面的 findNodeAtPosition

/**
 * @brief 辅助函数：根据位置查找最小的包含该位置的 CST 节点。(示例实现)
 * @param root CST 的根节点。
 * @param position 要查找的位置 (0-based)。
 * @return 指向最内层节点的指针，如果找不到则返回 nullptr。
 */
antlr4::tree::ParseTree *findNodeAtPosition(antlr4::tree::ParseTree *root, Position position) {
  if (!root)
    return nullptr;

  std::queue<antlr4::tree::ParseTree *> q;
  q.push(root);
  antlr4::tree::ParseTree *smallestNode = nullptr;
  long smallestSize = -1; // 使用 -1 表示尚未找到

  while (!q.empty()) {
    antlr4::tree::ParseTree *current = q.front();
    q.pop();

    antlr4::ParserRuleContext *ruleCtx = dynamic_cast<antlr4::ParserRuleContext *>(current);
    antlr4::tree::TerminalNode *termNode = dynamic_cast<antlr4::tree::TerminalNode *>(current);
    antlr4::Token *startToken = nullptr;
    antlr4::Token *stopToken = nullptr;
    long nodeSize = -1;

    if (ruleCtx) {
      startToken = ruleCtx->getStart();
      stopToken = ruleCtx->getStop();
      if (startToken && stopToken) {
        // 计算节点跨越的字符数（近似大小）
        nodeSize = static_cast<long>(stopToken->getStopIndex()) -
                   static_cast<long>(startToken->getStartIndex()) + 1;
      }
    } else if (termNode) {
      startToken = termNode->getSymbol();
      stopToken = startToken; // 终端节点只有一个 token
      if (startToken) {
        nodeSize = static_cast<long>(startToken->getStopIndex()) -
                   static_cast<long>(startToken->getStartIndex()) + 1;
      }
    }

    if (startToken && stopToken) {
      long startLine = static_cast<long>(startToken->getLine()) - 1;
      long startChar = static_cast<long>(startToken->getCharPositionInLine());
      long stopLine = static_cast<long>(stopToken->getLine()) - 1;
      long stopChar = static_cast<long>(stopToken->getCharPositionInLine()) +
                      static_cast<long>(stopToken->getText().length());

      // 检查 position 是否在节点的范围内
      bool containsPosition = false;
      if (position.line > startLine && position.line < stopLine) {
        containsPosition = true; // 在中间行
      } else if (position.line == startLine && position.line == stopLine) {
        // 同一行
        containsPosition = (position.character >= startChar && position.character <= stopChar);
      } else if (position.line == startLine) {
        // 在起始行
        containsPosition = (position.character >= startChar);
      } else if (position.line == stopLine) {
        // 在结束行
        containsPosition = (position.character <= stopChar);
      }

      if (containsPosition) {
        // 如果当前节点包含该位置，比较它的大小
        if (smallestNode == nullptr || nodeSize < smallestSize) {
          smallestNode = current;
          smallestSize = nodeSize;
        }

        // 将子节点加入队列继续搜索更小的节点
        for (auto child : current->children) {
          q.push(child);
        }
      }
    } else {
      // 如果没有有效的 token 信息，但当前节点可能包含目标，则检查子节点
      // (这种情况比较少见，除非是根节点或其他特殊节点)
      // 为了避免无限循环或错误，可以简单地将子节点加入队列
      for (auto child : current->children) {
        // 简单的检查避免空指针
        if (child)
          q.push(child);
      }
    }
  }

  return smallestNode; // 返回找到的最小包含节点
}

/**
 * @brief 实现悬停信息的核心逻辑。(需要符号表和 CST 节点信息)
 */
/**
 * @brief 实现悬停信息的核心逻辑。(修正 Range 获取方式)
 */
std::optional<Hover>
SptScriptAnalyzer::calculateHoverInfo(const std::shared_ptr<const AnalysisResult> &analysis,
                                      Position position) {
  PLOGD << "SptScriptAnalyzer::calculateHoverInfo";
  if (!analysis || !analysis->symbolTable || !analysis->parseRes->cstRoot) {
    return std::nullopt;
  }

  // 1. 查找光标位置对应的 CST 节点
  antlr4::tree::ParseTree *node = findNodeAtPosition(analysis->parseRes->cstRoot, position);
  if (!node) {
    PLOGV << " -> 未在位置处找到 CST 节点";
    return std::nullopt;
  }
  PLOGV << " -> 找到节点: " << node->toStringTree(static_cast<antlr4::Parser *>(nullptr), true)
        << " (" << typeid(*node).name() << ")";

  // 2. 确定符号 (TODO: 实现精确解析逻辑)
  SymbolInfoPtr symbol = nullptr;
  std::string symbolName;
  if (auto termNode = dynamic_cast<antlr4::tree::TerminalNode *>(node)) {
    if (termNode->getSymbol()->getType() == LangLexer::IDENTIFIER) {
      symbolName = termNode->getText();
      PLOGW << " -> 找到标识符终端节点 '" << symbolName << "'，但符号解析逻辑未实现";
      // 简化：尝试全局查找 (不准确)
      // symbol = analysis->symbolTable->resolveSymbol(symbolName);
    }
  }
  // TODO: 处理其他节点类型

  // 3. 如果找到符号，构建 Hover 信息
  if (symbol) {
    PLOGV << " -> 找到符号 '" << symbol->name << "'";
    Hover hover;
    std::string hoverText;

    // 添加类型信息
    if (symbol->type) {
      hoverText += "```sptscript\n";
      hoverText += "(" + symbolKindToString(symbol->kind) + ") " + symbol->name + ": " +
                   symbol->type->toString();
      hoverText += "\n```";
    } else { /* ... 处理未知类型 ... */
    }
    // TODO: 添加文档字符串

    hover.contents.kind = "markdown";
    hover.contents.value = hoverText;

    // *** 修正：在 Analyzer 内部计算 Range ***
    // 获取节点对应的 Range。这部分逻辑与 Visitor 中的 getRange/getTokenRange 类似。
    Range nodeRange = Range{Position{0, 0}, Position{0, 1}}; // 默认值
    if (auto termNode = dynamic_cast<antlr4::tree::TerminalNode *>(node)) {
      antlr4::Token *token = termNode->getSymbol();
      if (token) {
        long line = static_cast<long>(token->getLine()) - 1;
        long startChar = static_cast<long>(token->getCharPositionInLine());
        size_t tokenLength = token->getText().length();
        long endChar = startChar + static_cast<long>(std::max(static_cast<size_t>(1), tokenLength));
        line = std::max(0L, line);
        startChar = std::max(0L, startChar);
        endChar = std::max(startChar, endChar);
        nodeRange = Range{Position{line, startChar}, Position{line, endChar}};
      }
    } else if (auto ruleCtx = dynamic_cast<antlr4::ParserRuleContext *>(node)) {
      antlr4::Token *startToken = ruleCtx->getStart();
      antlr4::Token *stopToken = ruleCtx->getStop();
      if (startToken && stopToken) {
        long startLine = static_cast<long>(startToken->getLine()) - 1;
        long startChar = static_cast<long>(startToken->getCharPositionInLine());
        long stopLine = static_cast<long>(stopToken->getLine()) - 1;
        size_t stopTokenLength = stopToken->getText().length();
        long stopChar = static_cast<long>(stopToken->getCharPositionInLine()) +
                        static_cast<long>(std::max(static_cast<size_t>(1), stopTokenLength));
        startLine = std::max(0L, startLine);
        startChar = std::max(0L, startChar);
        stopLine = std::max(startLine, stopLine);
        if (stopLine == startLine) {
          stopChar = std::max(startChar, stopChar);
        } else {
          stopChar = std::max(0L, stopChar);
        }
        nodeRange = Range{Position{startLine, startChar}, Position{stopLine, stopChar}};
      } else if (startToken) { // 只有 start token
        long line = static_cast<long>(startToken->getLine()) - 1;
        long startChar = static_cast<long>(startToken->getCharPositionInLine());
        size_t tokenLength = startToken->getText().length();
        long endChar = startChar + static_cast<long>(std::max(static_cast<size_t>(1), tokenLength));
        line = std::max(0L, line);
        startChar = std::max(0L, startChar);
        endChar = std::max(startChar, endChar);
        nodeRange = Range{Position{line, startChar}, Position{line, endChar}};
      }
    }
    hover.range = nodeRange; // 设置悬停范围

    return hover;
  } else {
    PLOGV << " -> 未在该位置解析出符号";
  }

  return std::nullopt;
}

/**
 * @brief 获取指定位置的悬停信息。(调用核心逻辑)
 */
std::optional<Hover> SptScriptAnalyzer::getHoverInfo(const Uri &uri, Position position) {
  PLOGD << "SptScriptAnalyzer::getHoverInfo at " << uri.str() << ":" << position.line + 1 << ":"
        << position.character + 1;
  std::shared_ptr<const AnalysisResult> analysis = getCachedAnalysis(uri);
  if (!analysis || !analysis->semanticAnalysisDone) {
    PLOGW << " -> Cannot provide hover info: analysis not available or incomplete for "
          << uri.str();
    return std::nullopt;
  }
  return calculateHoverInfo(analysis, position);
}

// --- 代码补全 (Completion) ---

/**
 * @brief 辅助函数：在指定作用域及其父作用域中查找所有可见符号
 */
void findVisibleSymbols(std::shared_ptr<Scope> currentScope, std::vector<SymbolInfoPtr> &symbols) {
  if (!currentScope)
    return;

  // 添加当前作用域的符号
  for (const auto &pair : currentScope->symbols) {
    symbols.push_back(pair.second);
  }

  // 递归访问父作用域
  findVisibleSymbols(currentScope->getParent(), symbols);

  // TODO: 处理导入的符号 (如果它们没有直接合并到当前作用域)
}

/**
 * @brief 实现代码补全的核心逻辑。(需要大量上下文分析)
 */
// 新增辅助函数：尝试获取点号/冒号左侧表达式的类型
// 这是一个非常简化的版本，实际中会更复杂
TypeInfoPtr SptScriptAnalyzer::inferBaseExpressionTypeForMemberAccess(
    const std::shared_ptr<const AnalysisResult> &analysis,
    Position position, // 光标位置，在点号或冒号之后
    DocumentManager &docManager) {
  PLOGD << "inferBaseExpressionTypeForMemberAccess for URI: " << analysis->uri.str() << " at "
        << position.line << ":" << position.character;
  std::optional<DocumentState> docState = docManager.getDocument(analysis->uri);
  if (!docState)
    return UnknownType;

  // 1. 找到触发字符 ('.', ':') 的位置
  Position triggerCharPos = position;
  if (position.character == 0)
    return UnknownType; // 不应该发生

  if (triggerCharPos.character == 0) {
    // 如果点号在行首，前面肯定没有标识符（除非是换行后的链式调用，暂简化处理）
    // 但为了防止 getPrefixAtPosition 出错，这里可以做个检查
    // 实际上 getPrefixAtPosition 能够处理 character 0
  }

  // 2. 获取点或冒号之前的AST节点
  // 这是一个难点。理想情况下，我们应该从 `position` 向前回溯，找到构成基础表达式的token/节点。
  // 简化：我们尝试找到包含“点号前字符”的最小 IDENTIFIER 或 PostfixExp 节点。
  // 更好的方法是查看当前光标所在节点的父节点，如果是 PostfixMemberSuffixContext 等，
  // 它的第一个子节点就是基础表达式。

  // 假设我们可以通过某种方式得到基础表达式的最后一个Token或其ParseTree节点 (baseExpressionNode)
  // 例如，从光标位置的节点向上查找父节点，直到找到 PostfixExpressionContext，然后取其
  // primaryExp()。 这需要更复杂的AST导航。

  // 暂时使用一个非常简化的方法：尝试解析点号/冒号左边的标识符。
  // 这只适用于 `identifier.member` 的情况，不适用于 `func().member` 或 `list[0].member`。
  std::string baseIdentifierText =
      getPrefixAtPosition(docState->content, triggerCharPos); // 获取点号前的标识符
  if (baseIdentifierText.empty()) {
    PLOGW << " -> Could not extract base identifier for member access.";
    return UnknownType;
  }
  PLOGD << " -> Base identifier for member access: " << baseIdentifierText;

  // 3. 在符号表中查找该标识符
  // 需要找到这个标识符定义的作用域，或者从当前光标位置的作用域开始向上查找
  std::shared_ptr<Scope> scopeToSearch = findScopeAtPosition(*analysis, triggerCharPos);
  if (!scopeToSearch)
    scopeToSearch = analysis->symbolTable->getGlobalScope();

  if (scopeToSearch) {
    SymbolInfoPtr baseSymbol = scopeToSearch->resolve(baseIdentifierText); // 使用 resolve 向上查找
    if (baseSymbol && baseSymbol->type) {
      PLOGD << " -> Found base symbol '" << baseIdentifierText
            << "' with type: " << baseSymbol->type->toString();
      return baseSymbol->type;
    } else {
      PLOGW << " -> Could not resolve base symbol '" << baseIdentifierText
            << "' or it has no type.";
    }
  }
  return UnknownType;
}

std::vector<CompletionItem>
SptScriptAnalyzer::calculateCompletions(const std::shared_ptr<const AnalysisResult> &analysis,
                                        Position position, DocumentManager &docManager,
                                        const std::optional<CompletionContext> &lspContext) {
  PLOGD << "SptScriptAnalyzer::calculateCompletions for URI: " << analysis->uri.str() << " at L"
        << position.line << "C" << position.character;
  std::vector<CompletionItem> items;

  if (!analysis || !analysis->symbolTable || !analysis->parseRes->cstRoot ||
      !analysis->semanticAnalysisDone) {
    PLOGW << " -> Analysis not ready or incomplete for completion.";
    return items;
  }

  std::string prefix = "";
  char triggerChar = 0;         // 由 LSP 客户端指明的直接触发字符 (如 . : )
  bool invokedManually = false; // 是否是用户手动调用补全 (如 Ctrl+Space)

  // --- 1. 解析 LSP CompletionContext ---
  if (lspContext.has_value()) {
    const auto &ctx = lspContext.value();
    invokedManually = (ctx.triggerKind == CompletionTriggerKind::Invoked);
    if (ctx.triggerKind == CompletionTriggerKind::TriggerCharacter &&
        ctx.triggerCharacter.has_value() && !ctx.triggerCharacter.value().empty()) {
      triggerChar = ctx.triggerCharacter.value()[0];
      PLOGD << " -> Completion triggered by LSP context. TriggerChar: '" << triggerChar
            << "', Kind: " << static_cast<int>(ctx.triggerKind);
      // 当由触发字符引发时，前缀是光标前的标识符部分，在触发字符之后的内容通常不作为前缀。
      // 但 getPrefixAtPosition 会获取光标位置前的整个标识符。
      // 如果是 '.', 那么prefix应该是点后面的部分。
      // 我们让 getPrefixAtPosition 处理，它会看光标前的字符。
    } else {
      PLOGD << " -> Completion triggered by LSP context. Kind: "
            << static_cast<int>(ctx.triggerKind) << " (No specific char or manual invoke)";
    }
  } else {
    PLOGD << " -> No LSP CompletionContext provided. Assuming manual invocation or simple typing.";
    invokedManually = true; // 假设是手动或常规输入
  }

  // --- 2. 获取文档内容和精确的前缀 ---
  std::optional<DocumentState> docState = docManager.getDocument(analysis->uri);
  if (docState) {
    // 如果是触发字符，光标通常在触发字符之后，此时前缀应为空或用户已输入的部分
    // 如果是手动调用或普通输入，getPrefixAtPosition 会获取光标前的标识符
    prefix = getPrefixAtPosition(docState->content, position);
    PLOGD << " -> Extracted prefix from content: '" << prefix << "'";

    // 如果 LSP context 没有提供 triggerChar，我们再尝试从内容判断一次
    // 这主要用于兼容没有发送 context 的客户端或某些特殊情况
    if (triggerChar == 0 && position.character > 0 && !prefix.empty()) {
      size_t charBeforeCursorOffset = positionToOffset(docState->content, position);
      if (charBeforeCursorOffset > prefix.length()) { // 确保前缀之前还有字符
        char charBeforePrefix = docState->content[charBeforeCursorOffset - prefix.length() - 1];
        if (charBeforePrefix == '.')
          triggerChar = '.';
        else if (charBeforePrefix == ':')
          triggerChar = ':';
        if (triggerChar != 0) {
          PLOGD << " -> Deduced trigger char from content: '" << triggerChar << "'";
        }
      }
    }
  } else {
    PLOGW << " -> Could not get document content for accurate prefix/trigger detection.";
  }
  PLOGD << " -> Final effective Prefix: '" << prefix << "', Final Trigger Char: '" << triggerChar
        << "', Invoked Manually: " << invokedManually;

  // --- 3. 根据上下文生成补全项 ---

  // --- 3.1 成员访问补全 ('.', ':') ---
  if (triggerChar == '.' || triggerChar == ':') {
    PLOGD << " -> Member access completion triggered by '" << triggerChar << "'";
    // 点号或冒号前的位置
    long newCharPos = position.character - 1 - static_cast<long>(prefix.length());
    if (newCharPos < 0)
      newCharPos = 0; // 防止负值
    Position baseExprTriggerPos = {position.line, newCharPos};
    if (position.character == 0 || (position.character - 1 - prefix.length() < 0)) { // 防止越界
      PLOGW << " -> Invalid position for member access base expression.";
    } else {
      TypeInfoPtr baseType =
          inferBaseExpressionTypeForMemberAccess(analysis, baseExprTriggerPos, docManager);

      if (baseType && baseType != UnknownType && baseType != AnyType) {
        if (auto classType = std::dynamic_pointer_cast<ClassType>(baseType)) {
          PLOGD << " -> Base type is Class: " << classType->name;
          if (classType->classScope) {
            for (const auto &pair : classType->classScope->symbols) {
              const SymbolInfoPtr &memberSymbol = pair.second;
              if (memberSymbol && !memberSymbol->name.empty()) {
                bool showMember = true;   // 根据语言规范和上下文细化
                if (triggerChar == '.') { // '.' 通常访问实例成员
                  // 对于静态语言，严格来说，通过实例点号不应提示静态成员
                  // 但有些语言允许，或IDE为了方便会提示
                  // showMember = !memberSymbol->isStatic; // 严格模式
                } else if (triggerChar == ':') { // ':'
                  // 同样，静态或实例都可能，取决于语言规范
                }

                if (showMember && (prefix.empty() || memberSymbol->name.rfind(prefix, 0) == 0)) {
                  CompletionItem item;
                  item.label = memberSymbol->name;
                  item.kind = symbolToCompletionItemKind(memberSymbol->kind);
                  if (memberSymbol->type)
                    item.detail = memberSymbol->type->toString();
                  if (memberSymbol->documentation.has_value())
                    item.documentation =
                        MarkupContent{"markdown", memberSymbol->documentation.value()};

                  if (memberSymbol->kind == SymbolKind::FUNCTION) { // 假设方法也是FUNCTION kind
                    if (auto funcSig =
                            std::dynamic_pointer_cast<FunctionSignature>(memberSymbol->type)) {
                      std::string sigDetail = funcSig->toString();
                      item.detail = sigDetail;
                      std::string snippet = memberSymbol->name + "(";
                      for (size_t i = 0; i < funcSig->parameters.size(); ++i) {
                        snippet +=
                            "${" + std::to_string(i + 1) + ":" + funcSig->parameters[i].first + "}";
                        if (i < funcSig->parameters.size() - 1)
                          snippet += ", ";
                      }
                      snippet += ")";
                      // 如果是方法且没有参数，可以考虑加不加括号
                      // if(funcSig->parameters.empty()) snippet = memberSymbol->name;
                      item.insertText = snippet;
                      item.insertTextFormat = InsertTextFormat::Snippet;
                    }
                  }
                  items.push_back(std::move(item));
                  PLOGV << "   -> Added member completion item: " << item.label;
                }
              }
            }
          } else {
            PLOGW << " -> Class " << classType->name << " has no associated scope.";
          }
        }
        // TODO: 处理导入的模块 (如果它们被表示为某种可枚举成员的类型)
        // else if (auto moduleScope = std::dynamic_pointer_cast<Scope>(baseType)) { ... }
        else {
          PLOGW << " -> Base type " << baseType->toString()
                << " does not support member access for completion in current logic.";
        }
      } else {
        PLOGW
            << " -> Could not determine base type for member access, or base type is Any/Unknown.";
      }
    }
  } else { // --- 3.2 作用域内符号和关键字补全 ---
    PLOGD << " -> Scope-based completion for prefix: '" << prefix << "'";
    std::shared_ptr<Scope> currentScope = findScopeAtPosition(*analysis, position);
    if (!currentScope) {
      PLOGW << " -> Could not find scope at position for URI " << analysis->uri.str()
            << ", falling back to global.";
      currentScope = analysis->symbolTable->getGlobalScope();
    }

    if (currentScope) {
      PLOGD << " -> Starting completion search from scope kind: "
            << static_cast<int>(currentScope->kind) << " (Ptr: " << currentScope.get() << ")";
      std::vector<SymbolInfoPtr> visibleSymbols;
      findVisibleSymbols(currentScope, visibleSymbols);

      for (const auto &symbol : visibleSymbols) {
        if (symbol && !symbol->name.empty()) {
          if (prefix.empty() || symbol->name.rfind(prefix, 0) == 0) {
            CompletionItem item;
            item.label = symbol->name;
            item.kind = symbolToCompletionItemKind(symbol->kind);
            if (symbol->type) {
              item.detail = symbol->type->toString();
            }
            if (symbol->documentation.has_value()) {
              item.documentation = MarkupContent{"markdown", symbol->documentation.value()};
            }
            if (symbol->kind == SymbolKind::FUNCTION ||
                symbol->kind == SymbolKind::BUILTIN_FUNCTION) {
              if (auto funcSig = std::dynamic_pointer_cast<FunctionSignature>(symbol->type)) {
                item.detail = funcSig->toString(); // 替换纯类型为函数签名
                std::string snippet = symbol->name + "(";
                for (size_t i = 0; i < funcSig->parameters.size(); ++i) {
                  snippet +=
                      "${" + std::to_string(i + 1) + ":" + funcSig->parameters[i].first + "}";
                  if (i < funcSig->parameters.size() - 1)
                    snippet += ", ";
                }
                snippet += ")";
                item.insertText = snippet;
                item.insertTextFormat = InsertTextFormat::Snippet;
              }
            }
            items.push_back(std::move(item));
            PLOGV << "   -> Added scope completion item: " << item.label;
          }
        }
      }
    } else {
      PLOGW << " -> currentScope is null, cannot find visible symbols.";
    }

    // 添加关键字 (只有在用户可能输入新标识符，且不是在点号等操作符后时)
    if (triggerChar == 0) {
      const std::vector<std::string> keywords = {
          "if",    "else", "while",  "for",    "return",  "class",  "function", "export", "import",
          "from",  "as",   "type",   "int",    "float",   "string", "bool",     "any",    "void",
          "null",  "list", "map",    "union",  "tuple",   "true",   "false",    "this",   "new",
          "const", "auto", "global", "static", "mutivar", "coro"};
      for (const auto &kw : keywords) {
        if (prefix.empty() || kw.rfind(prefix, 0) == 0) {
          CompletionItem item;
          item.label = kw;
          item.kind = CompletionItemKind::Keyword;
          items.push_back(std::move(item));
          PLOGV << "   -> Added keyword item: " << item.label;
        }
      }
    }
  }

  PLOGI << " -> Generated " << items.size() << " completion items for prefix '" << prefix << "'.";
  return items;
}

/**
 * @brief 获取指定位置的代码补全建议。(调用核心逻辑)
 */
std::optional<CompletionList>
SptScriptAnalyzer::getCompletions(const Uri &uri, Position position, DocumentManager &docManager,
                                  const std::optional<CompletionContext> &lspContext) {
  PLOGD << "SptScriptAnalyzer::getCompletions at " << uri.str() << ":" << position.line + 1 << ":"
        << position.character + 1;
  std::shared_ptr<const AnalysisResult> analysis = getCachedAnalysis(uri);
  if (!analysis || !analysis->semanticAnalysisDone) { // 需要语义分析完成
    PLOGW << " -> Cannot provide completions: analysis not available or incomplete for "
          << uri.str();
    // 可以返回一个空的、不完整的列表
    CompletionList emptyList;
    emptyList.isIncomplete = true; // 表示结果不完整
    return emptyList;
    // return std::nullopt; // 或者完全不提供
  }

  std::vector<CompletionItem> items =
      calculateCompletions(analysis, position, docManager, lspContext);
  CompletionList list;
  list.items = std::move(items);
  // 根据 calculateCompletions 的实现逻辑决定是否 isIncomplete
  // 如果建议很多或处理被截断，可以设为 true
  list.isIncomplete = false;
  return list;
}

// 构造函数
SptScriptAnalyzer::SptScriptAnalyzer() { PLOGD << "SptScriptAnalyzer 实例已创建。"; }

// 安全地从缓存获取分析结果
std::shared_ptr<const AnalysisResult> SptScriptAnalyzer::getCachedAnalysis(const Uri &uri) {
  std::lock_guard<std::mutex> lock(cacheMtx); // 锁定缓存以进行读取
  auto it = analysisCache.find(uri);
  if (it != analysisCache.end()) {
    PLOGV << "SptScriptAnalyzer::getCachedAnalysis - 缓存命中: " << uri.str();
    return it->second; // 返回指向缓存结果的共享指针
  }
  PLOGV << "SptScriptAnalyzer::getCachedAnalysis - 缓存未命中: " << uri.str();
  return nullptr; // 未找到
}

// 清除指定 URI 的分析缓存
void SptScriptAnalyzer::clearAnalysis(const Uri &uri) {
  std::lock_guard<std::mutex> lock(cacheMtx); // 锁定缓存以进行写入
  if (analysisCache.erase(uri) > 0) {
    PLOGI << "SptScriptAnalyzer::clearAnalysis - 已清除缓存: " << uri.str();
  } else {
    PLOGD << "SptScriptAnalyzer::clearAnalysis - 尝试清除不存在的缓存: " << uri.str();
  }
}

// 分析文档 (主入口)
std::shared_ptr<const AnalysisResult>
SptScriptAnalyzer::analyzeDocument(const Uri &uri, const std::string &content, long version) {
  PLOGI << "SptScriptAnalyzer::analyzeDocument - 请求分析: " << uri.str() << " (版本: " << version
        << ")";

  // 1. 检查缓存
  {
    std::lock_guard<std::mutex> lock(cacheMtx);
    auto it = analysisCache.find(uri);
    if (it != analysisCache.end() && it->second && it->second->version == version) {
      PLOGI << " -> 使用缓存版本 " << version << " 的分析结果: " << uri.str();
      return it->second;
    }
    PLOGI << " -> 缓存未命中或版本不匹配 (请求: " << version << ", 缓存: "
          << (it != analysisCache.end() && it->second ? std::to_string(it->second->version) : "N/A")
          << "): " << uri.str();
  }

  // 2. 执行新的分析
  PLOGI << " -> 执行新分析: " << uri.str();
  // *** 注意： analyzeDocument 的这个重载不接受 visitedInCurrentStack ***
  // *** 它需要自己创建这个 set 并传递给 performAnalysis ***
  std::set<Uri> visitedInCurrentStack;
  std::shared_ptr<const AnalysisResult> constResult =
      performAnalysis(uri, content, version, visitedInCurrentStack);

  // 3. 更新缓存
  if (constResult) {
    std::lock_guard<std::mutex> lock(cacheMtx);
    PLOGI << " -> 更新缓存: " << uri.str() << " (版本: " << version << ")";
    analysisCache[uri] = constResult;
  } else {
    PLOGE << " -> 分析失败，无法更新缓存: " << uri.str();
    clearAnalysis(uri); // 如果分析失败，清除旧缓存
  }
  return constResult;
}

// 重载 analyzeDocument 以接受 visitedInStack (供内部递归调用)
// 这个函数主要由 SemanticAnalyzerVisitor::processImport 调用
std::shared_ptr<const AnalysisResult>
SptScriptAnalyzer::analyzeDocument(const Uri &uri, const std::string &content, long version,
                                   std::set<Uri> &visitedInCurrentStack) {
  PLOGI << "SptScriptAnalyzer::analyzeDocument (recursive) - 请求分析: " << uri.str();

  // 1. 检查缓存 (即使在递归中也检查缓存)
  {
    std::lock_guard<std::mutex> lock(cacheMtx);
    auto it = analysisCache.find(uri);
    // 递归调用时通常不关心版本号
    // 或者需要更复杂的依赖管理机制
    if (it != analysisCache.end() && it->second) {
      PLOGI << " -> (递归) 使用缓存的分析结果: " << uri.str();
      // 需要检查循环导入！如果缓存的结果尚未完成语义分析，且当前路径已在栈中，则有问题。
      // 但 performAnalysis 内部会做检查，所以这里可以直接返回。
      return it->second;
    }
    PLOGI << " -> (递归) 缓存未命中: " << uri.str();
  }

  // 2. 读取文件内容 (如果 content 为空)
  std::string actual_content = content;
  if (actual_content.empty()) {
    PLOGV << " -> (递归) 读取文件内容 for " << uri.str();
    auto contentOpt = readFileContent(uri); // 使用私有辅助函数
    if (!contentOpt) {
      PLOGE << " -> (递归) 无法读取文件内容: " << uri.str();
      return nullptr; // 返回 nullptr 表示失败
    }
    actual_content = contentOpt.value();
  }

  // 3. 执行实际分析 (调用 performAnalysis)
  PLOGI << " -> (递归) 执行新分析: " << uri.str();
  std::shared_ptr<const AnalysisResult> constResult =
      performAnalysis(uri, actual_content, version, visitedInCurrentStack);

  // 4. 更新缓存 (同上)
  if (constResult) {
    std::lock_guard<std::mutex> lock(cacheMtx);
    PLOGI << " -> (递归) 更新缓存: " << uri.str();
    analysisCache[uri] = constResult;
  } else {
    PLOGE << " -> (递归) 分析失败，无法更新缓存: " << uri.str();
    clearAnalysis(uri);
  }
  return constResult;
}

/**
 * @brief 执行实际的解析和语义分析（私有辅助函数）。
 */
std::shared_ptr<const AnalysisResult>
SptScriptAnalyzer::performAnalysis(const Uri &uri, const std::string &content, long version,
                                   std::set<Uri> &visitedInCurrentStack, // 注意是引用传递
                                   bool forceReanalysis /*= false*/) {
  PLOGI << "SptScriptAnalyzer::performAnalysis - 开始执行分析: " << uri.str();

  // 1. 循环导入检测
  if (visitedInCurrentStack.count(uri)) {
    PLOGE << "检测到循环导入！分析中止于: " << uri.str();
    // 可以考虑向调用者（通过某种方式）报告循环导入错误
    return nullptr; // 返回 nullptr 表示分析失败
  }
  visitedInCurrentStack.insert(uri);
  PLOGV << " -> 加入分析栈: " << uri.str();

  // RAII Helper: 确保函数退出时移除 URI
  struct CycleGuard {
    std::set<Uri> &visited;
    const Uri &currentUri;
    bool removed = false;

    CycleGuard(std::set<Uri> &v, const Uri &u) : visited(v), currentUri(u) {}

    ~CycleGuard() { remove(); }

    void remove() {
      if (!removed) {
        visited.erase(currentUri);
        removed = true;
        PLOGV << " <- 离开分析栈: " << currentUri.str();
      }
    }
  };

  CycleGuard cycleGuard(visitedInCurrentStack, uri);

  // 2. 执行解析
  PLOGD << " -> 调用 ParserEngine::parse for " << uri.str();
  std::shared_ptr<ParseResult> parseResult = ParserEngine::parse(content, uri.str());

  // 3. 创建分析结果对象
  auto analysisResult = std::make_shared<AnalysisResult>(uri, version, std::move(parseResult));

  // 4. 如果 CST 成功生成，则运行语义分析通道
  if (analysisResult->parseRes->cstRoot != nullptr) {
    try {
      PLOGD << " -> 开始对 " << uri.str() << " 运行语义分析通道...";
      runSemanticPass(*analysisResult, visitedInCurrentStack); // 传入非 const 引用以填充结果
      analysisResult->semanticAnalysisDone = true;
      PLOGI << " -> 语义分析通道完成 for " << uri.str() << ", 发现 "
            << analysisResult->semanticDiagnostics.size() << " 个语义问题。";
    } catch (const std::exception &e) {
      PLOGF << " -> 语义分析通道发生严重异常 for " << uri.str() << ": " << e.what();
      Diagnostic diag;
      diag.message = "内部语义分析器错误: " + std::string(e.what());
      diag.range = Range{Position{0, 0}, Position{0, 1}};
      diag.severity = DiagnosticSeverity::Error;
      diag.source = "semantic";
      analysisResult->semanticDiagnostics.push_back(std::move(diag));
      analysisResult->semanticAnalysisDone = false;
    } catch (...) {
      PLOGF << " -> 语义分析通道发生未知类型的严重异常 for " << uri.str();
      Diagnostic diag;
      diag.message = "未知的内部语义分析器错误";
      diag.range = Range{Position{0, 0}, Position{0, 1}};
      diag.severity = DiagnosticSeverity::Error;
      diag.source = "semantic";
      analysisResult->semanticDiagnostics.push_back(std::move(diag));
      analysisResult->semanticAnalysisDone = false;
    }
  } else {
    PLOGW << " -> 由于 CST 根节点为空，跳过语义分析 for " << uri.str();
    analysisResult->semanticAnalysisDone = false;
  }

  // 5. CycleGuard 自动析构
  // 6. 返回结果
  PLOGI << "SptScriptAnalyzer::performAnalysis - 完成分析: " << uri.str();
  return std::const_pointer_cast<const AnalysisResult>(analysisResult);
}

// 运行语义分析通道 (创建并运行 Visitor)
void SptScriptAnalyzer::runSemanticPass(
    AnalysisResult &result,
    std::set<Uri> &visitedInCurrentStack) { // ** 参数改为 AnalysisResult& **
  if (!result.parseRes->cstRoot) {
    PLOGW << "runSemanticPass - 跳过，CST 根节点为空: " << result.uri.str();
    return;
  }
  if (!result.symbolTable) {
    PLOGE << "runSemanticPass - 严重错误: 符号表未初始化: " << result.uri.str();
    return;
  }

  PLOGD << "runSemanticPass - 创建 SemanticAnalyzerVisitor for " << result.uri.str();
  SemanticAnalyzerVisitor visitor(*(result.symbolTable), result.semanticDiagnostics, result.uri,
                                  *this, visitedInCurrentStack,
                                  result // ** 传递非 const 引用 **
  );

  PLOGD << "runSemanticPass - 开始遍历 CST for " << result.uri.str();
  try {
    visitor.visit(result.parseRes->cstRoot);
  } catch (const std::exception &e) {
    PLOGF << "runSemanticPass - Visitor 遍历期间发生未捕获的标准异常 for " << result.uri.str()
          << ": " << e.what();
    // 不建议在这里 re-throw，因为可能导致整个分析流程中断
    // 可以在这里记录一个顶层错误诊断
    Diagnostic diag;
    diag.message = "内部语义分析器错误 (遍历): " + std::string(e.what());
    diag.range = Range{Position{0, 0}, Position{0, 1}};
    diag.severity = DiagnosticSeverity::Error;
    diag.source = "semantic";
    result.semanticDiagnostics.push_back(std::move(diag));
    result.semanticAnalysisDone = false; // 标记语义分析未完全成功
  } catch (...) {
    PLOGF << "runSemanticPass - Visitor 遍历期间发生未捕获的未知异常 for " << result.uri.str();
    Diagnostic diag;
    diag.message = "未知的内部语义分析器错误 (遍历)";
    diag.range = Range{Position{0, 0}, Position{0, 1}};
    diag.severity = DiagnosticSeverity::Error;
    diag.source = "semantic";
    result.semanticDiagnostics.push_back(std::move(diag));
    result.semanticAnalysisDone = false;
  }
  PLOGD << "runSemanticPass - Visitor 遍历完成 for " << result.uri.str();
}

// --- 实现 SptScriptAnalyzer::findScopeAtPosition ---
std::shared_ptr<Scope> SptScriptAnalyzer::findScopeAtPosition(const AnalysisResult &analysis,
                                                              Position position) {
  PLOGV << "findScopeAtPosition at " << position.line + 1 << ":"
        << position.character + 1; // Line + 1 for human-readable log
  if (!analysis.symbolTable) {
    PLOGW << " -> 符号表无效，无法查找作用域";
    return nullptr;
  }

  std::shared_ptr<Scope> bestScope = nullptr;
  long smallestRangeLineSpan = -1;
  long smallestRangeCharSpan = -1;

  // 遍历范围映射 (std::map 已经按 Range 的 operator< 排序)
  for (const auto &pair : analysis.scopeRangeMap) {
    const Range &range = pair.first;

    // 严格的范围检查 (Position is 0-based)
    // 1. Must be on or after start line
    // 2. Must be before end line, OR on end line and before end character
    // 3. If on start line, must be on or after start character
    bool contains = false;
    if (position.line > range.start.line) {
      if (position.line < range.end.line) {
        contains = true; // Strictly between start and end lines
      } else if (position.line == range.end.line) {
        contains = (position.character < range.end.character); // On end line, before end char
      }
    } else if (position.line == range.start.line) {
      if (position.line < range.end.line) {
        contains = (position.character >= range.start.character); // On start line, after start char
      } else if (position.line == range.end.line) {
        // On the same line for start and end
        contains = (position.character >= range.start.character &&
                    position.character < range.end.character);
      }
    }

    if (contains) {
      // 计算范围大小（优先选择行跨度小，其次选择列跨度小的）
      long currentLineSpan = range.end.line - range.start.line;
      long currentCharSpan = -1; // Only calculate if needed
      // 简单的启发式大小比较：行数优先，然后是字符数
      // 注意：对于嵌套作用域，更小的范围通常意味着更小的行/列跨度

      // 第一次找到包含的范围，或者当前范围比已找到的最佳范围更小
      if (bestScope == nullptr || currentLineSpan < smallestRangeLineSpan) {
        currentCharSpan = (range.end.line == range.start.line)
                              ? (range.end.character - range.start.character)
                              : 100000; // Assign a large char span for multi-line ranges initially
                                        // if line span is the primary sort key
        bestScope = pair.second;
        smallestRangeLineSpan = currentLineSpan;
        smallestRangeCharSpan = currentCharSpan;
        PLOGV << "  -> 候选作用域更新 (行跨度优先): Kind " << static_cast<int>(bestScope->kind)
              << " Range [" << range.start.line << ":" << range.start.character << "-"
              << range.end.line << ":" << range.end.character << "]";
      } else if (currentLineSpan == smallestRangeLineSpan) {
        // 行跨度相同，比较字符跨度 (只有在单行时精确比较才有意义)
        currentCharSpan = (range.end.line == range.start.line)
                              ? (range.end.character - range.start.character)
                              : 100000; // Assign a large char span for multi-line ranges initially
        if (currentCharSpan < smallestRangeCharSpan) {
          bestScope = pair.second;
          smallestRangeCharSpan = currentCharSpan; // 更新最小字符跨度
          PLOGV << "  -> 候选作用域更新 (字符跨度优先): Kind " << static_cast<int>(bestScope->kind)
                << " Range [" << range.start.line << ":" << range.start.character << "-"
                << range.end.line << ":" << range.end.character << "]";
        }
      }
    }
  }

  if (!bestScope) {
    PLOGV << " -> 未在 scopeRangeMap 中找到包含该位置的作用域，回退到全局作用域";
    bestScope = analysis.symbolTable->getGlobalScope(); // 回退到全局作用域
  } else {
    PLOGV << " -> 最终找到最内层作用域 Kind " << static_cast<int>(bestScope->kind);
  }

  return bestScope;
}

// --- 实现 SptScriptAnalyzer::findVisibleSymbols ---
void SptScriptAnalyzer::findVisibleSymbols(std::shared_ptr<Scope> startingScope,
                                           std::vector<SymbolInfoPtr> &results) {
  PLOGV << "findVisibleSymbols starting from Scope Kind "
        << (startingScope ? static_cast<int>(startingScope->kind) : -1);
  results.clear();
  std::unordered_set<std::string> addedNames; // 用于处理遮蔽

  std::shared_ptr<Scope> current = startingScope;
  while (current) {
    PLOGV << "  -> 检查 Scope Kind " << static_cast<int>(current->kind);
    // 添加当前作用域的符号
    for (const auto &pair : current->symbols) {
      const std::string &name = pair.first;
      // 如果这个名字还没被内层作用域添加过
      if (addedNames.find(name) == addedNames.end()) {
        results.push_back(pair.second);
        addedNames.insert(name);
        PLOGV << "    -> 添加符号: " << name;
      } else {
        PLOGV << "    -> 跳过被遮蔽的符号: " << name;
      }
    }
    // 移动到父作用域
    current = current->getParent();
  }

  // 可选: 在这里添加全局内置函数和类型到 results (如果它们不在全局作用域的 symbols 里)
  // 例如，遍历一个预定义的内置符号列表，如果名字不在 addedNames 中，就添加

  PLOGD << "findVisibleSymbols 完成，找到 " << results.size() << " 个可见符号。";
}

// 解析导入路径 (基础相对路径实现)
std::optional<Uri> SptScriptAnalyzer::resolveImportPath(const Uri &currentUri,
                                                        const std::string &importString) {
  PLOGV << "resolveImportPath - 尝试解析导入 '" << importString << "' (相对于 " << currentUri.str()
        << ")";
  if (!currentUri.isValid() || importString.empty()) {
    PLOGW << "...解析失败：当前 URI 无效或导入字符串为空。";
    return std::nullopt;
  }

  try {
    // 假设 currentUri.path() 返回的是平台相关的路径字符串
    std::filesystem::path currentPath =
        std::filesystem::u8path(currentUri.path()); // 使用 u8path 处理 UTF-8 路径
    std::filesystem::path currentDir = currentPath.parent_path();
    std::filesystem::path importedPath;

    // 替换 '.' 为路径分隔符 (根据 SptScript 规范或常见做法) - 如果需要的话
    // std::string normalizedImportString = importString;
    // std::replace(normalizedImportString.begin(), normalizedImportString.end(), '.',
    // std::filesystem::path::preferred_separator); std::filesystem::path importPathFragment =
    // std::filesystem::u8path(normalizedImportString);

    // 这里假设 importString 就是相对路径或绝对路径（需要处理）
    std::filesystem::path importPathFragment = std::filesystem::u8path(importString);

    // TODO: 实现更健壮的路径解析逻辑，可能需要考虑工作区根目录、库路径等
    // 简化：仅支持相对路径
    if (importPathFragment.is_relative()) {
      importedPath = currentDir / importPathFragment;
    } else {
      // 如果是绝对路径，直接使用（或者根据策略禁止？）
      // importedPath = importPathFragment;
      PLOGE << "...解析失败：当前仅支持相对路径导入: '" << importString << "'";
      return std::nullopt;
    }

    importedPath = std::filesystem::weakly_canonical(importedPath); // 规范化路径, 解析 ".." 等

    // 检查文件存在性，并尝试添加 .flx 后缀
    std::error_code ec;
    bool exists = std::filesystem::is_regular_file(importedPath, ec);
    if (!exists || ec) {
      std::filesystem::path pathWithExt = importedPath;
      pathWithExt += ".flx"; // 直接附加，或者使用 replace_extension
      exists = std::filesystem::is_regular_file(pathWithExt, ec);
      if (exists && !ec) {
        importedPath = pathWithExt; // 使用带后缀的路径
      } else {
        PLOGW << "...解析失败：文件不存在或不是常规文件 (已尝试 .flx 后缀): "
              << importedPath.string();
        return std::nullopt;
      }
    }

    // 将本地路径转换回 file URI (需要更健壮的实现，考虑编码等)
    std::string resolvedPathStr = importedPath.generic_u8string(); // 获取通用 UTF-8 路径
    // 确保路径以 / 开头 (对于 Unix-like) 或 C:/ (对于 Windows)
    // Windows: C:\path\to -> /C:/path/to
#ifdef _WIN32
    if (resolvedPathStr.length() > 0 && resolvedPathStr[0] != '/') {
      resolvedPathStr.insert(0, "/"); // 添加前缀斜杠
    }
    std::replace(resolvedPathStr.begin(), resolvedPathStr.end(), '\\', '/');
#else
    if (resolvedPathStr.empty() || resolvedPathStr[0] != '/') {
      resolvedPathStr.insert(0, "/");
    }
#endif

    // TODO: 进行 URL 百分号编码 (处理空格、特殊字符等)
    std::string resolvedUriString = "file://" + resolvedPathStr; // 简化拼接，未编码

    PLOGI << "resolveImportPath - 解析导入 '" << importString << "' -> " << resolvedUriString;
    return Uri(resolvedUriString);

  } catch (const std::filesystem::filesystem_error &e) {
    PLOGE << "...文件系统错误: " << e.what();
    return std::nullopt;
  } catch (const std::exception &e) {
    PLOGE << "...异常: " << e.what();
    return std::nullopt;
  } catch (...) {
    PLOGE << "...未知异常";
    return std::nullopt;
  }
}

// 从文件系统读取文件内容
std::optional<std::string> SptScriptAnalyzer::readFileContent(const Uri &uri) {
  if (!uri.isValid()) {
    PLOGE << "readFileContent - 尝试读取无效 URI: " << uri.str();
    return std::nullopt;
  }
  // 假设 uri.path() 返回的是适合平台的文件路径
  const std::string &filePathStr = uri.path();
  PLOGD << "readFileContent - 尝试读取文件: " << filePathStr;

  // 使用 C++17 filesystem 获取路径
  std::filesystem::path filePath = std::filesystem::u8path(filePathStr);

  // 检查文件是否存在且是常规文件
  std::error_code ec;
  if (!std::filesystem::is_regular_file(filePath, ec)) {
    if (ec) {
      PLOGE << "...检查文件状态时出错: " << ec.message();
    } else {
      PLOGE << "...文件不存在或不是常规文件";
    }
    return std::nullopt;
  }

  // 以二进制模式读取文件内容
  std::ifstream fileStream(filePath, std::ios::binary | std::ios::ate); // ate: 打开并定位到末尾
  if (!fileStream) {
    PLOGE << "...无法打开文件: " << strerror(errno); // 使用 errno 获取错误信息
    return std::nullopt;
  }

  std::streamsize size = fileStream.tellg(); // 获取文件大小
  fileStream.seekg(0, std::ios::beg);        // 回到文件开头

  if (size < 0 || !fileStream) { // 检查 tellg/seekg 是否成功
    PLOGE << "...无法定位或获取文件大小";
    return std::nullopt;
  }

  if (size == 0) {
    return ""; // 空文件
  }

  // 读取内容到字符串
  std::string buffer(static_cast<size_t>(size), '\0'); // 创建足够大的 buffer
  if (fileStream.read(buffer.data(), size)) {          // data() 在 C++17 中保证可写
    PLOGD << "...成功读取文件 (大小: " << buffer.length() << ")";
    return buffer;
  } else {
    PLOGE << "...读取文件时发生 I/O 错误";
    return std::nullopt;
  }
}

// --- LSP Feature Implementations ---

/**
 * @brief 获取指定文档的所有诊断信息。(修正：使用 getCachedAnalysis)
 */
std::vector<Diagnostic> SptScriptAnalyzer::getDiagnostics(const Uri &uri) {
  PLOGD << "SptScriptAnalyzer::getDiagnostics for " << uri.str();
  std::vector<Diagnostic> allDiagnostics;
  // *** 修正：调用私有辅助函数 getCachedAnalysis ***
  std::shared_ptr<const AnalysisResult> analysis = getCachedAnalysis(uri);
  if (analysis) {
    allDiagnostics.insert(allDiagnostics.end(), analysis->parseRes->syntaxDiagnostics.begin(),
                          analysis->parseRes->syntaxDiagnostics.end());
    allDiagnostics.insert(allDiagnostics.end(), analysis->semanticDiagnostics.begin(),
                          analysis->semanticDiagnostics.end());
    PLOGV << " -> Found " << analysis->parseRes->syntaxDiagnostics.size() << " syntax + "
          << analysis->semanticDiagnostics.size() << " semantic diagnostics in cache.";
  } else {
    PLOGW << " -> No cached analysis result found for diagnostics: " << uri.str();
  }
  return allDiagnostics;
}

/**
 * @brief 实现查找定义的核心逻辑。(修正：移除 getTokenRange/getRange 调用)
 */
std::optional<DefinitionLink>
SptScriptAnalyzer::findDefinitionLocation(const std::shared_ptr<const AnalysisResult> &analysis,
                                          Position position, DocumentManager &docManager) {
  PLOGD << "SptScriptAnalyzer::findDefinitionLocation";
  if (!analysis || !analysis->symbolTable || !analysis->parseRes->cstRoot)
    return std::nullopt;
  antlr4::tree::ParseTree *node = findNodeAtPosition(analysis->parseRes->cstRoot, position);
  if (!node) {
    PLOGV << " -> 未在位置处找到 CST 节点";
    return std::nullopt;
  }
  PLOGV << " -> 找到节点: " << node->toStringTree(static_cast<antlr4::Parser *>(nullptr), true)
        << " (" << typeid(*node).name() << ")";
  SymbolInfoPtr symbol = nullptr;
  std::string symbolName;

  std::shared_ptr<Scope> scopeToSearch = findScopeAtPosition(*analysis, position);
  if (!scopeToSearch)
    scopeToSearch = analysis->symbolTable->getGlobalScope();

  TerminalNode *termNode = nullptr;

  if (termNode = dynamic_cast<TerminalNode *>(node)) {
    if (termNode->getSymbol()->getType() == LangLexer::IDENTIFIER) {
      symbolName = termNode->getText();
      symbol = analysis->symbolTable->resolveSymbol(symbolName);
      PLOGW << " -> 找到标识符终端节点 '" << symbolName << "'，但符号解析逻辑未实现";
    }
  } else if (auto typeCtx = dynamic_cast<LangParser::TypeQualifiedIdentifierContext *>(node)) {
    termNode = typeCtx->qualifiedIdentifier()->IDENTIFIER()[0];
    if (termNode->getSymbol()->getType() == LangLexer::IDENTIFIER) {
      symbolName = termNode->getText();
      symbol = scopeToSearch->resolve(symbolName);
      if (auto classType = dynamic_cast<ClassType *>(symbol->type.get())) {
        auto *def = static_cast<LangParser::ClassDeclarationDefContext *>(classType->definitionNode);
        auto startToken = def->getStart();
        auto stopToken = def->getStop();
        return DefinitionLink{
            getRange(termNode), symbol->definitionLocation.uri,
            Range{
                Position{(long)startToken->getLine() - 1,
                         (long)startToken->getCharPositionInLine()},
                Position{(long)stopToken->getLine() - 1, (long)stopToken->getCharPositionInLine()},
            },
            symbol->definitionLocation.range};
      };
      PLOGW << " -> 找到标识符终端节点 '" << symbolName << "'，但符号解析逻辑未实现";
    }
  }
  if (symbol) {
    PLOGI << " -> 找到符号 '" << symbol->name << "' 的定义";
    if (!symbol->definitionLocation.uri.empty()) {
      return DefinitionLink{getRange(termNode), symbol->definitionLocation.uri,
                            symbol->definitionLocation.range, symbol->definitionLocation.range};
    } else {
      PLOGW << " -> 符号 '" << symbol->name << "' 缺少有效的定义位置信息";
    }
  } else {
    PLOGV << " -> 未能在该位置解析出符号";
  }
  return std::nullopt;
}

/**
 * @brief 查找指定位置符号的定义。(修正：使用 getCachedAnalysis)
 */
std::optional<DefinitionLink> SptScriptAnalyzer::findDefinition(const Uri &uri, Position position,
                                                                DocumentManager &docManager) {
  PLOGD << "SptScriptAnalyzer::findDefinition at " << uri.str() << ":" << position.line + 1 << ":"
        << position.character + 1;
  // *** 修正：调用私有辅助函数 getCachedAnalysis ***
  std::shared_ptr<const AnalysisResult> analysis = getCachedAnalysis(uri);
  if (!analysis || !analysis->semanticAnalysisDone) {
    PLOGW << " -> Cannot find definition: analysis not available or incomplete for " << uri.str();
    return std::nullopt;
  }
  return findDefinitionLocation(analysis, position, docManager);
}
