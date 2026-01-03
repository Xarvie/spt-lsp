#include "semantic_info.h" // 包含对应的头文件声明
#include <algorithm>       // 用于 std::sort, std::unique, std::all_of, std::any_of
#include <memory>          // 用于 std::shared_ptr
#include <set>             // 用于辅助 UnionType 去重比较
#include <sstream>         // 用于 std::stringstream 构建字符串
#include <stdexcept>       // 用于 std::runtime_error 抛出异常
#include <utility>         // 用于 std::move
#include <vector>          // 用于 std::vector

// --- 引入 plog 日志库 ---
#include <plog/Log.h>

// --- TypeInfo isEqualTo 实现 ---

/**
 * @brief 辅助函数：比较两个 TypeInfoPtr 指向的类型是否相等。
 * 处理 nullptr 的情况，并调用虚函数进行实际比较。
 * @param a 第一个类型指针。
 * @param b 第二个类型指针。
 * @return 如果指向的类型相等则返回 true，否则 false。
 */
bool compareTypePtrs(const TypeInfoPtr &a, const TypeInfoPtr &b) {
  if (a == nullptr && b == nullptr) {
    // 两个都为空指针，视为不相等（无类型不等于无类型）
    return false;
  }
  if (a == nullptr || b == nullptr) {
    // 一个为空指针，一个不为空，必然不等。
    return false;
  }
  // 两个指针都非空，调用虚函数比较它们指向的对象。
  return a->isEqualTo(*b);
}

// BaseType 的构造函数
BaseType::BaseType(BaseTypeKind k) : kind(k) {}

// BaseType 的 isEqualTo 实现
bool BaseType::isEqualTo(const TypeInfo &other) const {
  // 使用 dynamic_cast 安全地检查 other 是否也是 BaseType
  if (const auto *otherBase = dynamic_cast<const BaseType *>(&other)) {
    // 如果转换成功，比较它们的类型种类 (kind) 是否相同
    return this->kind == otherBase->kind;
  }
  // 如果 other 的动态类型不是 BaseType，则它们不相等
  return false;
}

// ListType 的构造函数
ListType::ListType(TypeInfoPtr elem) : elementType(std::move(elem)) {
  // 如果在构造时未提供元素类型，则默认元素类型为 'any'
  if (!elementType) {
    elementType = std::make_shared<BaseType>(BaseTypeKind::ANY);
  }
}

// ListType 的 isEqualTo 实现
bool ListType::isEqualTo(const TypeInfo &other) const {
  if (const auto *otherList = dynamic_cast<const ListType *>(&other)) {
    // 如果 other 也是 ListType，则递归比较它们的元素类型
    return compareTypePtrs(this->elementType, otherList->elementType);
  }
  return false;
}

// MapType 的构造函数
MapType::MapType(TypeInfoPtr key, TypeInfoPtr value)
    : keyType(std::move(key)), valueType(std::move(value)) {
  // 如果未提供键或值类型，则默认为 'any'
  if (!keyType) {
    keyType = std::make_shared<BaseType>(BaseTypeKind::ANY);
  }
  if (!valueType) {
    valueType = std::make_shared<BaseType>(BaseTypeKind::ANY);
  }
}

// MapType 的 isEqualTo 实现
bool MapType::isEqualTo(const TypeInfo &other) const {
  if (const auto *otherMap = dynamic_cast<const MapType *>(&other)) {
    // 如果 other 也是 MapType，则需要键类型和值类型都分别递归相等
    return compareTypePtrs(this->keyType, otherMap->keyType) &&
           compareTypePtrs(this->valueType, otherMap->valueType);
  }
  return false;
}

// UnionType 构造函数（包含验证和规范化）
UnionType::UnionType(std::vector<TypeInfoPtr> members) : memberTypes(std::move(members)) {
  // 1. 检查初始成员数量是否符合规范（至少两个）
  if (memberTypes.size() < 2) {
    PLOGE << "语义错误: 尝试创建成员少于 2 个的 Union 类型。";
    throw std::runtime_error("语义错误: union 类型必须至少包含 2 个成员类型。");
  }
  // 2. 检查是否有 null 类型指针
  if (std::any_of(memberTypes.begin(), memberTypes.end(),
                  [](const TypeInfoPtr &p) { return p == nullptr; })) {
    PLOGE << "语义错误: Union 类型的成员不能为 null 类型指针。";
    throw std::runtime_error("语义错误: union 类型的成员不能为 null 类型指针。");
  }

  // 3. 基础规范化：移除重复类型
  // 定义比较函数，用于排序和 unique
  auto typePtrLess = [](const TypeInfoPtr &a, const TypeInfoPtr &b) {
    // 注意：基于 toString 排序可能并非在所有情况下都绝对可靠，
    // 但它提供了一个确定性的排序标准，足以用于去重。
    return a->toString() < b->toString();
  };
  auto typePtrEqual = [](const TypeInfoPtr &a, const TypeInfoPtr &b) {
    // 使用实现的 isEqualTo 进行精确比较
    return a->isEqualTo(*b);
  };

  // 先排序
  std::sort(this->memberTypes.begin(), this->memberTypes.end(), typePtrLess);
  // 再移除连续的重复项
  this->memberTypes.erase(
      std::unique(this->memberTypes.begin(), this->memberTypes.end(), typePtrEqual),
      this->memberTypes.end());

  // 4. 再次检查成员数量是否仍然 >= 2
  if (this->memberTypes.size() < 2) {
    // 例如 union<int, int> 会变成只有一个 int
    PLOGE << "语义错误: Union 类型在移除重复成员后少于 2 个有效成员类型。";
    throw std::runtime_error("语义错误: union 类型在移除重复成员后少于 2 个成员类型。");
  }
  PLOG_VERBOSE << "成功创建并规范化 UnionType: " << this->toString();
}

// UnionType 的 isEqualTo 实现
bool UnionType::isEqualTo(const TypeInfo &other) const {
  if (const auto *otherUnion = dynamic_cast<const UnionType *>(&other)) {
    // 假设两者都已在构造时规范化（排序、去重）
    if (this->memberTypes.size() != otherUnion->memberTypes.size())
      return false;
    // 因为已排序，只需逐个比较对应元素
    for (size_t i = 0; i < this->memberTypes.size(); ++i) {
      // 注意 compareTypePtrs 处理了 nullptr 的情况 (虽然构造时已检查)
      if (!compareTypePtrs(this->memberTypes[i], otherUnion->memberTypes[i]))
        return false;
    }
    return true;
  }
  return false;
}

// TupleType 构造函数（包含验证）
TupleType::TupleType(std::vector<TypeInfoPtr> elements) : elementTypes(std::move(elements)) {
  // 根据 SptScript 规范检查元素数量
  if (elementTypes.size() < 2 || elementTypes.size() > 16) {
    PLOGE << "语义错误: tuple 类型必须包含 2 到 16 个元素类型，实际数量: " << elementTypes.size();
    throw std::runtime_error("语义错误: tuple 类型必须包含 2 到 16 个元素类型，实际数量: " +
                             std::to_string(elementTypes.size()));
  }
  // 检查是否有 null 类型指针
  if (std::any_of(elementTypes.begin(), elementTypes.end(),
                  [](const TypeInfoPtr &p) { return p == nullptr; })) {
    PLOGE << "语义错误: tuple 类型的元素不能为 null 类型指针。";
    throw std::runtime_error("语义错误: tuple 类型的元素不能为 null 类型指针。");
  }
  PLOG_VERBOSE << "成功创建 TupleType: " << this->toString();
}

// TupleType 的 isEqualTo 实现
bool TupleType::isEqualTo(const TypeInfo &other) const {
  if (const auto *otherTuple = dynamic_cast<const TupleType *>(&other)) {
    // 比较元组类型：数量相同且对应类型相同
    if (this->elementTypes.size() != otherTuple->elementTypes.size())
      return false;
    for (size_t i = 0; i < this->elementTypes.size(); ++i) {
      if (!compareTypePtrs(this->elementTypes[i], otherTuple->elementTypes[i]))
        return false;
    }
    return true;
  }
  return false;
}

// FunctionSignature 的 isEqualTo 实现
bool FunctionSignature::isEqualTo(const TypeInfo &other) const {
  if (const auto *otherFunc = dynamic_cast<const FunctionSignature *>(&other)) {
    // 比较签名：参数数量、类型（按顺序）、可变参数、返回类型
    if (this->isVariadic != otherFunc->isVariadic)
      return false;
    if (this->parameters.size() != otherFunc->parameters.size())
      return false;
    for (size_t i = 0; i < this->parameters.size(); ++i) {
      // 只比较类型，忽略参数名
      if (!compareTypePtrs(this->parameters[i].second, otherFunc->parameters[i].second))
        return false;
    }
    // 比较返回类型 (variant)
    if (this->returnTypeInfo.index() != otherFunc->returnTypeInfo.index())
      return false; // 单一 vs 多返回标记
    if (std::holds_alternative<TypeInfoPtr>(this->returnTypeInfo)) {
      // 都是单一返回类型，递归比较
      return compareTypePtrs(std::get<TypeInfoPtr>(this->returnTypeInfo),
                             std::get<TypeInfoPtr>(otherFunc->returnTypeInfo));
    } else {
      // 都是 MultiReturnTag，视为相等
      return true;
    }
  }
  return false;
}

// ClassType 的构造函数
ClassType::ClassType(std::string n, std::shared_ptr<Scope> scope, antlr4::tree::ParseTree *defNode)
    : name(std::move(n)), classScope(std::move(scope)), definitionNode(defNode) {}

// ClassType 的 isEqualTo 实现 (基于名称的名义类型系统)
bool ClassType::isEqualTo(const TypeInfo &other) const {
  if (const auto *otherClass = dynamic_cast<const ClassType *>(&other)) {
    // 类类型相等，当且仅当它们的名称相同
    return this->name == otherClass->name;
  }
  return false;
}

// --- TypeInfo toString 实现 ---

// BaseType 的 toString 实现
std::string BaseType::toString() const {
  switch (kind) {
  case BaseTypeKind::UNKNOWN:
    return "<unknown>";
  case BaseTypeKind::ANY:
    return "any";
  case BaseTypeKind::VOID:
    return "void";
  case BaseTypeKind::NULL_TYPE:
    return "null";
  case BaseTypeKind::INT:
    return "int";
  case BaseTypeKind::FLOAT:
    return "float";
  case BaseTypeKind::NUMBER:
    return "number";
  case BaseTypeKind::BOOL:
    return "bool";
  case BaseTypeKind::STRING:
    return "string";
  case BaseTypeKind::FUNCTION:
    return "function";
  case BaseTypeKind::COROUTINE:
    return "coro";
  default:
    return "<invalid_base_type>";
  }
}

// ListType 的 toString 实现
std::string ListType::toString() const {
  return "list<" + (elementType ? elementType->toString() : "any") + ">";
}

// MapType 的 toString 实现
std::string MapType::toString() const {
  return "map<" + (keyType ? keyType->toString() : "any") + ", " +
         (valueType ? valueType->toString() : "any") + ">";
}

// UnionType 的 toString 实现
std::string UnionType::toString() const {
  std::stringstream ss;
  ss << "union<";
  // 假定 memberTypes 在构造时已排序和去重，且不含 nullptr
  for (size_t i = 0; i < memberTypes.size(); ++i) {
    ss << memberTypes[i]->toString();
    if (i < memberTypes.size() - 1)
      ss << ", ";
  }
  ss << ">";
  return ss.str();
}

// TupleType 的 toString 实现
std::string TupleType::toString() const {
  std::stringstream ss;
  ss << "tuple<";
  // 假定 elementTypes 在构造时不含 nullptr
  for (size_t i = 0; i < elementTypes.size(); ++i) {
    ss << elementTypes[i]->toString();
    if (i < elementTypes.size() - 1)
      ss << ", ";
  }
  ss << ">";
  return ss.str();
}

// FunctionSignature 的 toString 实现
std::string FunctionSignature::toString() const {
  std::stringstream ss;
  ss << "function(";
  for (size_t i = 0; i < parameters.size(); ++i) {
    // 通常参数类型指针不应为 null，如果是则表示分析有错误
    ss << parameters[i].first << ": "
       << (parameters[i].second ? parameters[i].second->toString() : "<error_type>");
    if (i < parameters.size() - 1)
      ss << ", ";
  }
  if (isVariadic) {
    if (!parameters.empty())
      ss << ", ";
    ss << "...";
  }
  ss << ") -> ";
  std::visit(
      [&ss](auto &&arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, TypeInfoPtr>) {
          ss << (arg ? arg->toString() : "void"); // 假设 null 返回类型指针代表 void
        } else if constexpr (std::is_same_v<T, FunctionSignature::MultiReturnTag>) {
          ss << "mutivar"; // 使用关键字表示多返回
        }
      },
      returnTypeInfo);
  return ss.str();
}

// ClassType 的 toString 实现
std::string ClassType::toString() const { return name; }

// --- SymbolInfo 方法实现 ---

// SymbolInfo 构造函数
SymbolInfo::SymbolInfo(std::string n, SymbolKind k, TypeInfoPtr t, Location defLoc,
                       std::shared_ptr<Scope> parentScope, antlr4::tree::ParseTree *defNode)
    : name(std::move(n)), kind(k), type(std::move(t)), definitionLocation(std::move(defLoc)),
      definitionNode(defNode), scope(parentScope) // 从 shared_ptr 构造 weak_ptr
{}

// 获取定义此符号的作用域
std::shared_ptr<Scope> SymbolInfo::getScope() const {
  return scope.lock(); // 尝试从 weak_ptr 获取 shared_ptr
}

// --- Scope 方法实现 ---

// Scope 构造函数
Scope::Scope(ScopeKind k, std::shared_ptr<Scope> p, antlr4::tree::ParseTree *owner)
    : kind(k), parent(p), ownerNode(owner) {
  // 使用详细日志记录作用域创建
  PLOGD << "创建新作用域: 类型=" << static_cast<int>(kind)
        << ", 父作用域=" << (p ? std::to_string(static_cast<int>(p->kind)) : "null")
        << ", 关联节点=" << (owner ? "存在" : "不存在");
}

// 获取父作用域
std::shared_ptr<Scope> Scope::getParent() const {
  return parent.lock(); // lock() 从 weak_ptr 获取 shared_ptr (如果父对象还存在)
}

// 在当前作用域定义符号
bool Scope::define(SymbolInfoPtr symbol) {
  if (!symbol || symbol->name.empty()) {
    PLOG_WARNING << "尝试在作用域 (类型 " << static_cast<int>(kind) << ") 中定义无效或匿名的符号。";
    return false;
  }
  // 使用 try_emplace 尝试插入，如果键已存在则不会插入，并返回 false
  auto [iterator, success] = symbols.try_emplace(symbol->name, symbol);

  if (!success) {
    // 符号已存在，记录错误信息（包含位置）
    // 注意：实际的错误添加应由调用此方法的 Visitor 完成，因为它有更完整的上下文
    PLOG_WARNING << "符号 '" << symbol->name << "' 在当前作用域 (类型 " << static_cast<int>(kind)
                 << ") 已存在，定义失败。"
                 << " (新定义位置: " << symbol->definitionLocation.range.start.line + 1 << ":"
                 << symbol->definitionLocation.range.start.character + 1
                 << ", 已存在位置: " << iterator->second->definitionLocation.range.start.line + 1
                 << ":" << iterator->second->definitionLocation.range.start.character + 1 << ")";
  } else {
    PLOG_VERBOSE << "在作用域 (类型 " << static_cast<int>(kind) << ") 中成功定义符号 '"
                 << symbol->name << "'";
  }
  return success;
}

// 在当前作用域及其父作用域链中查找符号
SymbolInfoPtr Scope::resolve(const std::string &name) const {
  PLOG_VERBOSE << "在作用域 (类型 " << static_cast<int>(kind) << ") 中查找符号 '" << name << "'";
  auto it = symbols.find(name);
  if (it != symbols.end()) {
    PLOG_VERBOSE << " -> 在当前作用域找到符号 '" << name << "'";
    return it->second; // 在当前作用域找到
  }
  // 如果存在父作用域，则递归查找
  if (auto p = parent.lock()) {
    PLOG_VERBOSE << " -> 在当前作用域未找到，向上查找父作用域 (类型 " << static_cast<int>(p->kind)
                 << ")";
    return p->resolve(name);
  }
  // 到达顶层仍未找到
  PLOG_VERBOSE << " -> 在作用域链中未找到符号 '" << name << "'";
  return nullptr;
}

// 仅在当前作用域中查找符号
SymbolInfoPtr Scope::resolveLocally(const std::string &name) const {
  PLOG_VERBOSE << "在作用域 (类型 " << static_cast<int>(kind) << ") 中本地查找符号 '" << name
               << "'";
  auto it = symbols.find(name);
  if (it != symbols.end()) {
    PLOG_VERBOSE << " -> 在本地作用域找到符号 '" << name << "'";
    return it->second;
  }
  PLOG_VERBOSE << " -> 在本地作用域未找到符号 '" << name << "'";
  return nullptr;
}

// 添加一个子作用域
void Scope::addChild(std::shared_ptr<Scope> child) {
  if (child) {
    PLOG_VERBOSE << "向作用域 (类型 " << static_cast<int>(kind) << ") 添加子作用域 (类型 "
                 << static_cast<int>(child->kind) << ")";
    children.push_back(child);
    // 设置子作用域的父指针 (使用 weak_ptr 赋值)
    // 需要确保当前 Scope 对象是通过 shared_ptr 管理的，这由 SymbolTable 保证
    child->parent = this->shared_from_this();
  } else {
    PLOG_WARNING << "尝试向作用域添加空的子作用域指针。";
  }
}