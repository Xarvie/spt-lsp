#include "semantic_info.h" // 包含 SymbolTable, Scope, SymbolInfo 等声明
#include <memory>          // 用于 std::make_shared
#include <stdexcept>       // 用于 std::runtime_error
#include <string>          // 用于 std::string

// --- 引入 plog 日志库 ---
#include <plog/Log.h>

/**
 * @brief SymbolTable 构造函数。
 * 初始化符号表，创建全局作用域并将其设置为当前作用域。
 * 全局作用域没有父作用域，也不关联特定的 CST 节点。
 */
SymbolTable::SymbolTable() {
  // 使用 make_shared 创建 Scope 对象，并初始化 globalScope
  globalScope = std::make_shared<Scope>(ScopeKind::GLOBAL, nullptr, nullptr);
  // 初始时，当前作用域就是全局作用域
  currentScope = globalScope;
  // 记录符号表创建事件，通常使用 DEBUG 或 INFO 级别
  PLOGD << "符号表已创建，全局作用域已初始化。";
}

/**
 * @brief 进入一个新的作用域 (将新作用域设置为当前作用域，并维护父子关系)。
 * @param kind 要创建的新作用域的类型 (FUNCTION, CLASS, BLOCK 等)。
 * @param ownerNode (可选) 指向定义此作用域的 CST 节点。
 */
void SymbolTable::pushScope(ScopeKind kind, antlr4::tree::ParseTree *ownerNode) {
  // 检查当前作用域是否有效，防止在无效状态下操作
  if (!currentScope) {
    PLOGE << "严重错误：尝试在无效的(null)当前作用域上推入新作用域。";
    // 抛出异常，因为这是一个不可恢复的内部状态错误
    throw std::runtime_error("SymbolTable::pushScope called with null currentScope");
  }
  PLOG_VERBOSE << "进入新作用域，类型: " << static_cast<int>(kind)
               << "，父作用域类型: " << static_cast<int>(currentScope->kind);

  // 创建新的作用域实例，其父作用域是当前的 currentScope
  // 使用 make_shared 来创建共享指针管理新作用域的生命周期
  std::shared_ptr<Scope> newScope = std::make_shared<Scope>(kind, currentScope, ownerNode);

  // 将新作用域添加为当前作用域的子作用域
  // addChild 内部会设置新作用域的 parent (weak_ptr)
  currentScope->addChild(newScope);

  // 将新创建的作用域设置为当前的 currentScope
  currentScope = newScope;
}

/**
 * @brief 退出当前作用域，将当前作用域设置为其父作用域。
 * 如果当前已是全局作用域，则忽略此操作并记录警告。
 */
void SymbolTable::popScope() {
  // 检查当前作用域是否有效
  if (!currentScope) {
    PLOG_ERROR << "严重错误：尝试在无效的(null)当前作用域上执行 popScope。";
    throw std::runtime_error("SymbolTable::popScope called on null currentScope");
  }

  // 获取当前作用域的父作用域 (通过 weak_ptr 的 lock() 获取 shared_ptr)
  std::shared_ptr<Scope> parentScope = currentScope->getParent();

  if (parentScope) {
    PLOG_VERBOSE << "退出作用域 (类型 " << static_cast<int>(currentScope->kind)
                 << ")，返回到父作用域 (类型 " << static_cast<int>(parentScope->kind) << ")";
    // 将当前作用域指针更新为父作用域指针
    currentScope = parentScope;
  } else {
    // 如果父作用域为空指针，说明当前作用域是全局作用域
    PLOG_WARNING << "警告：尝试弹出全局作用域，操作被忽略。";
    // 全局作用域通常不应该被弹出
  }
}

/**
 * @brief 获取当前正在处理的作用域。
 * @return 指向当前作用域的共享指针。如果符号表未正确初始化，可能返回 nullptr。
 */
std::shared_ptr<Scope> SymbolTable::getCurrentScope() const { return currentScope; }

/**
 * @brief 获取全局（根）作用域。
 * @return 指向全局作用域的共享指针。
 */
std::shared_ptr<Scope> SymbolTable::getGlobalScope() const { return globalScope; }

/**
 * @brief 在当前作用域中定义一个符号。
 * @param symbol 要定义的符号信息的共享指针。
 * @return 如果定义成功（例如，没有名称冲突），返回 true；否则返回 false。
 */
bool SymbolTable::defineSymbol(SymbolInfoPtr symbol) {
  // 检查当前作用域和符号指针是否有效
  if (!currentScope) {
    PLOG_ERROR << "错误：尝试在无效的(null)当前作用域中定义符号。";
    return false;
  }
  if (!symbol) {
    PLOG_WARNING << "尝试在作用域 (类型 " << static_cast<int>(currentScope->kind)
                 << ") 中定义空的符号指针。";
    return false;
  }

  PLOG_VERBOSE << "在作用域 (类型 " << static_cast<int>(currentScope->kind) << ") 中尝试定义符号 '"
               << symbol->name << "'";
  // 设置符号的反向指针 (weak_ptr) 指向当前作用域
  symbol->scope = currentScope;
  // 调用当前作用域的 define 方法来实际存储符号并处理冲突
  bool success = currentScope->define(symbol);
  if (!success) {
    // 如果定义失败（例如名称冲突），可以在这里添加额外的日志或处理
    PLOG_INFO << "符号 '" << symbol->name << "' 定义失败（可能已存在于当前作用域）。";
  }
  return success;
}

/**
 * @brief 从当前作用域开始，沿着父作用域链向上查找符号。
 * @param name 要查找的符号名称。
 * @return 如果找到符号，返回指向符号信息的共享指针；否则返回 nullptr。
 */
SymbolInfoPtr SymbolTable::resolveSymbol(const std::string &name) const {
  if (!currentScope) {
    PLOG_WARNING << "尝试在无效的(null)当前作用域中查找符号 '" << name << "'";
    return nullptr;
  }
  // 调用当前作用域的 resolve 方法，该方法会递归向上查找
  SymbolInfoPtr foundSymbol = currentScope->resolve(name);
  if (foundSymbol) {
    PLOG_VERBOSE << "符号 '" << name << "' 已找到。";
  } else {
    PLOG_VERBOSE << "符号 '" << name << "' 未在当前作用域链中找到。";
  }
  return foundSymbol;
}

/**
 * @brief 将 SymbolKind 枚举转换为可读的字符串。
 */
std::string symbolKindToString(SymbolKind kind) {
  switch (kind) {
  case SymbolKind::VARIABLE:
    return "variable";
  case SymbolKind::PARAMETER:
    return "parameter";
  case SymbolKind::FUNCTION:
    return "function";
  case SymbolKind::CLASS:
    return "class";
  case SymbolKind::MODULE:
    return "module";
  case SymbolKind::TYPE_ALIAS:
    return "type alias";
  case SymbolKind::BUILTIN_FUNCTION:
    return "builtin function";
  case SymbolKind::BUILTIN_TYPE:
    return "builtin type";
  case SymbolKind::UNKNOWN:
    return "unknown";
  default:
    return "<invalid symbol kind>";
  }
}