#ifndef SEMANTIC_INFO_H
#define SEMANTIC_INFO_H

#include "protocol_types.h" // 需要 Position, Range, Location
#include "uri.h"
#include <memory> // for std::shared_ptr, std::unique_ptr
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

// #include "antlr4-runtime.h" // 可能需要 antlr4::tree::ParseTree* 等

// 前置声明
namespace antlr4::tree {
class ParseTree;
}            // namespace antlr4::tree
class Scope; // 作用域

// --- 类型系统基础结构 ---

/**
 * @brief 表示 SptScript 中各种类型信息的抽象基类。
 */
struct TypeInfo {
  // 虚拟析构函数，确保派生类可以被正确删除
  virtual ~TypeInfo() = default;

  /**
   * @brief 返回类型的字符串表示形式，主要用于调试和悬停提示。
   * @return 类型的字符串描述。
   */
  virtual std::string toString() const = 0;

  /**
   * @brief (新增声明) 检查当前类型是否与另一个类型相等。
   * @param other 要比较的另一个 TypeInfo 对象。
   * @return 如果类型相等则返回 true，否则返回 false。
   * @note 这是纯虚函数，需要在每个派生类中具体实现比较逻辑。
   */
  virtual bool isEqualTo(const TypeInfo &other) const = 0;

  // (可选) 克隆方法
  // virtual std::unique_ptr<TypeInfo> clone() const = 0;
};

/**
 * @brief 表示 SptScript 的内建基础类型种类。
 */
enum class BaseTypeKind {
  UNKNOWN,
  ANY,
  VOID,
  NULL_TYPE,
  INT,
  FLOAT,
  NUMBER,
  BOOL,
  STRING,
  FUNCTION,
  COROUTINE
};

/**
 * @brief 表示基础类型。
 */
struct BaseType : TypeInfo {
  BaseTypeKind kind = BaseTypeKind::UNKNOWN;
  explicit BaseType(BaseTypeKind k); // 构造函数声明
  std::string toString() const override;
  bool isEqualTo(const TypeInfo &other) const override; // 重写 isEqualTo
};

// 使用共享指针来表示类型
using TypeInfoPtr = std::shared_ptr<TypeInfo>;

/**
 * @brief 表示列表类型。
 */
struct ListType : TypeInfo {
  TypeInfoPtr elementType;
  explicit ListType(TypeInfoPtr elem = nullptr); // 构造函数声明
  std::string toString() const override;
  bool isEqualTo(const TypeInfo &other) const override; // 重写 isEqualTo
};

/**
 * @brief 表示映射类型。
 */
struct MapType : TypeInfo {
  TypeInfoPtr keyType;
  TypeInfoPtr valueType;
  MapType(TypeInfoPtr key = nullptr, TypeInfoPtr value = nullptr); // 构造函数声明
  std::string toString() const override;
  bool isEqualTo(const TypeInfo &other) const override; // 重写 isEqualTo
};

/**
 * @brief 表示联合类型。
 */
struct UnionType : TypeInfo {
  std::vector<TypeInfoPtr> memberTypes;
  explicit UnionType(std::vector<TypeInfoPtr> members); // 构造函数声明
  std::string toString() const override;
  bool isEqualTo(const TypeInfo &other) const override; // 重写 isEqualTo
};

/**
 * @brief 表示元组类型。
 */
struct TupleType : TypeInfo {
  std::vector<TypeInfoPtr> elementTypes;
  explicit TupleType(std::vector<TypeInfoPtr> elements); // 构造函数声明
  std::string toString() const override;
  bool isEqualTo(const TypeInfo &other) const override; // 重写 isEqualTo
};

/**
 * @brief 表示函数签名类型。
 */
struct FunctionSignature : TypeInfo {
  struct MultiReturnTag {}; // 空结构体标记多返回

  std::vector<std::pair<std::string, TypeInfoPtr>> parameters;
  std::variant<TypeInfoPtr, MultiReturnTag> returnTypeInfo;
  bool isVariadic = false;
  FunctionSignature() = default; // 默认构造函数
  std::string toString() const override;
  bool isEqualTo(const TypeInfo &other) const override; // 重写 isEqualTo
};

/**
 * @brief 表示用户定义的类类型。
 */
struct ClassType : TypeInfo {
  std::string name;
  std::shared_ptr<Scope> classScope; // 指向类成员定义的作用域
  antlr4::tree::ParseTree *definitionNode = nullptr;
  explicit ClassType(std::string n, std::shared_ptr<Scope> scope = nullptr,
                     antlr4::tree::ParseTree *defNode = nullptr); // 构造函数声明
  std::string toString() const override;
  bool isEqualTo(const TypeInfo &other) const override; // 重写 isEqualTo
};

// --- 符号系统基础结构 ---

/**
 * @brief 定义符号的种类。
 */
enum class SymbolKind {
  VARIABLE,
  PARAMETER,
  FUNCTION,
  CLASS,
  MODULE,
  TYPE_ALIAS,
  BUILTIN_FUNCTION,
  BUILTIN_TYPE,
  UNKNOWN
};

/**
 * @brief 存储单个符号的详细信息。
 */
struct SymbolInfo {
  std::string name;
  SymbolKind kind = SymbolKind::UNKNOWN;
  TypeInfoPtr type;
  Location definitionLocation;
  antlr4::tree::ParseTree *definitionNode = nullptr;
  std::weak_ptr<Scope> scope; // 使用 weak_ptr 避免循环引用
  bool isConst = false;
  bool isGlobal = false;
  bool isStatic = false;
  bool isExported = false;
  std::optional<std::string> documentation;

  // 构造函数
  SymbolInfo(std::string n, SymbolKind k, TypeInfoPtr t, Location defLoc,
             std::shared_ptr<Scope> parentScope = nullptr, // 接收 shared_ptr
             antlr4::tree::ParseTree *defNode = nullptr);  // 构造函数声明

  std::shared_ptr<Scope> getScope() const; // 声明
};

// 使用共享指针管理符号信息
using SymbolInfoPtr = std::shared_ptr<SymbolInfo>;

/**
 * @brief 定义作用域的类型。
 */
enum class ScopeKind { GLOBAL, MODULE, FUNCTION, CLASS, BLOCK };

/**
 * @brief 表示一个作用域，管理符号和子作用域。
 */
class Scope : public std::enable_shared_from_this<Scope> { // 继承以支持 shared_from_this
public:
  ScopeKind kind;
  std::weak_ptr<Scope> parent;                            // 指向父作用域
  std::vector<std::shared_ptr<Scope>> children;           // 包含的子作用域
  std::unordered_map<std::string, SymbolInfoPtr> symbols; // 当前作用域定义的符号
  antlr4::tree::ParseTree *ownerNode = nullptr;           // 定义此作用域的 CST 节点

  // 构造函数
  explicit Scope(ScopeKind k, std::shared_ptr<Scope> p = nullptr,
                 antlr4::tree::ParseTree *owner = nullptr); // 构造函数声明

  // 禁止拷贝
  Scope(const Scope &) = delete;
  Scope &operator=(const Scope &) = delete;

  // 方法声明
  std::shared_ptr<Scope> getParent() const;
  bool define(SymbolInfoPtr symbol);
  SymbolInfoPtr resolve(const std::string &name) const;
  SymbolInfoPtr resolveLocally(const std::string &name) const;
  void addChild(std::shared_ptr<Scope> child);
};

/**
 * @brief 符号表类，管理作用域栈和符号查找。
 */
class SymbolTable {
private:
  std::shared_ptr<Scope> globalScope;  // 全局作用域 (根)
  std::shared_ptr<Scope> currentScope; // 当前指向的作用域

public:
  // 构造函数声明
  SymbolTable();

  // 方法声明
  void pushScope(ScopeKind kind, antlr4::tree::ParseTree *ownerNode = nullptr);
  void popScope();
  std::shared_ptr<Scope> getCurrentScope() const;
  std::shared_ptr<Scope> getGlobalScope() const;
  bool defineSymbol(SymbolInfoPtr symbol);
  SymbolInfoPtr resolveSymbol(const std::string &name) const;
};

// --- 类型系统辅助函数/类 (占位符) ---
// (根据需要可以在单独的 type_system.h/cpp 中实现)
// bool isTypeAssignable(const TypeInfoPtr& source, const TypeInfoPtr& target);
// TypeInfoPtr inferExpressionType(antlr4::tree::ParseTree* expressionNode, SymbolTable& table);
/**
 * @brief 将 SymbolKind 枚举转换为可读的字符串。
 * @param kind 要转换的符号种类。
 * @return 对应的字符串表示。
 */
std::string symbolKindToString(SymbolKind kind);
#endif // SEMANTIC_INFO_H