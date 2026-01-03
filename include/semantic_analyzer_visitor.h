#ifndef SEMANTIC_ANALYZER_VISITOR_H
#define SEMANTIC_ANALYZER_VISITOR_H

// --- ANTLR 和项目头文件 ---
#include "LangParser.h" // !! 直接包含 Parser 头文件以获取所有 Context 类定义 !!
#include "LangParserBaseVisitor.h" // 引入 ANTLR 生成的 Visitor 基类
#include "protocol_types.h"        // 引入 LSP 协议类型 (主要需要 Diagnostic)
#include "semantic_info.h"         // 引入类型、符号、作用域等定义
#include "uri.h"                   // 引入 Uri 类
// (替代了大量的单独前置声明)

// --- 标准库头文件 ---
#include <any>    // 用于 visit 方法的返回值
#include <memory> // 用于智能指针
#include <set>    // 用于跟踪导入循环
#include <stack>  // 用于管理上下文状态
#include <string> // 用于字符串处理
#include <vector> // 用于存储诊断信息

// --- 前置声明 ---
class SptScriptAnalyzer; // 需要访问主分析器以递归处理导入
struct AnalysisResult;

/**
 * @brief CST 遍历器，用于执行 SptScript 的语义分析。
 * 继承自 ANTLR 生成的 BaseVisitor，通过重写 visit 方法实现分析逻辑。
 */
class SemanticAnalyzerVisitor : public LangParserBaseVisitor {
private:
  SymbolTable &symbolTable;             // 引用符号表，用于定义和查找符号
  std::vector<Diagnostic> &diagnostics; // 引用诊断列表，用于添加语义错误/警告
  const Uri &documentUri;               // 当前正在分析的文档的 URI
  SptScriptAnalyzer &analyzer; // 引用主分析器，用于解析 import 和获取依赖分析结果
  std::set<Uri> &visitedInStack; // 引用当前调用栈中正在分析的 URI 集合，用于检测循环导入
  AnalysisResult &analysisResult; // ** 新增: 引用分析结果以填充映射 **
  // --- 语义分析上下文状态 ---
  std::stack<TypeInfoPtr> expectedReturnTypeStack; // 跟踪当前函数预期的返回类型
  bool isInStaticMethod = false;                   // 标记当前是否在静态方法内部
  ClassType *currentClassType = nullptr; // 指向当前正在分析的类的类型信息 (原始指针)

  // --- 私有辅助方法 (将在 .cpp 中实现) ---
  Range getRange(antlr4::ParserRuleContext *ctx);
  Range getRange(antlr4::tree::TerminalNode *node);
  Range getTokenRange(antlr4::Token *token);
  Location getLocation(antlr4::ParserRuleContext *ctx);
  void addDiagnostic(const Range &range, const std::string &message,
                     DiagnosticSeverity severity = DiagnosticSeverity::Error);
  TypeInfoPtr resolveTypeFromContext(LangParser::TypeContext *ctx); // 解析类型注解
  std::shared_ptr<Scope> processImport(const std::string &uriString,
                                       antlr4::tree::ParseTree *importNode); // 处理导入语句
  bool checkTypeAssignable(const TypeInfoPtr &source, const TypeInfoPtr &target,
                           antlr4::tree::ParseTree *assignmentNode); // 检查类型兼容性
  TypeInfoPtr inferExpressionType(LangParser::ExpressionContext *ctx); // 推断表达式类型 (核心)
  //  辅助函数记录作用域范围 ---
  /**
   * @brief 记录当前作用域及其对应的源代码范围。
   * @param scope 要记录的作用域。
   * @param ownerNode 定义该作用域的 CST 节点。
   */
  void recordScopeRange(std::shared_ptr<Scope> scope, antlr4::tree::ParseTree *ownerNode);

  TypeInfoPtr getTypeFromNode(antlr4::tree::ParseTree *node); // 辅助函数：获取已访问节点的推断类型
  Range getTokenRange(antlr4::Token *startToken, antlr4::Token *stopToken);
  Location getLocation(antlr4::tree::TerminalNode *node);

public:
  /**
   * @brief 构造函数。
   * 初始化对符号表、诊断列表、URI、主分析器和访问栈的引用。
   */
  SemanticAnalyzerVisitor(SymbolTable &table, std::vector<Diagnostic> &diags, const Uri &uri,
                          SptScriptAnalyzer &analyzerRef, std::set<Uri> &visited,
                          AnalysisResult &resultRef);

  // --- 重写 visit 方法 (使用您提供的精确列表) ---

  // 顶层和块
  std::any visitCompilationUnit(LangParser::CompilationUnitContext *ctx) override;
  std::any visitBlockStatement(LangParser::BlockStatementContext *ctx) override;

  // 语句
  std::any visitSemicolonStmt(LangParser::SemicolonStmtContext *ctx) override;
  std::any visitAssignStmt(LangParser::AssignStmtContext *ctx) override;
  std::any visitUpdateStmt(LangParser::UpdateStmtContext *ctx) override;
  std::any visitExpressionStmt(LangParser::ExpressionStmtContext *ctx) override;
  std::any visitDeclarationStmt(LangParser::DeclarationStmtContext *ctx) override;
  std::any visitIfStmt(LangParser::IfStmtContext *ctx) override;
  std::any visitWhileStmt(LangParser::WhileStmtContext *ctx) override;
  std::any visitForStmt(LangParser::ForStmtContext *ctx) override;
  std::any visitBreakStmt(LangParser::BreakStmtContext *ctx) override;
  std::any visitContinueStmt(LangParser::ContinueStmtContext *ctx) override;
  std::any visitReturnStmt(LangParser::ReturnStmtContext *ctx) override;
  std::any visitBlockStmt(LangParser::BlockStmtContext *ctx) override;

  // 赋值
  std::any visitUpdateAssignStmt(LangParser::UpdateAssignStmtContext *ctx) override;
  std::any visitNormalAssignStmt(LangParser::NormalAssignStmtContext *ctx) override;
  std::any visitLvalueBase(LangParser::LvalueBaseContext *ctx) override;

  // 声明
  std::any visitDeclaration(LangParser::DeclarationContext *ctx) override;
  std::any visitVariableDeclarationDef(LangParser::VariableDeclarationDefContext *ctx) override;
  std::any visitFunctionDeclarationDef(LangParser::FunctionDeclarationDefContext *ctx) override;
  std::any visitClassDeclarationDef(LangParser::ClassDeclarationDefContext *ctx) override;
  std::any visitClassFieldMember(LangParser::ClassFieldMemberContext *ctx) override;
  std::any visitClassMethodMember(LangParser::ClassMethodMemberContext *ctx) override;
  std::any visitClassEmptyMember(LangParser::ClassEmptyMemberContext *ctx) override;
  std::any visitParameter(LangParser::ParameterContext *ctx) override;
  std::any visitParameterList(LangParser::ParameterListContext *ctx) override;

  // 类型
  std::any visitTypePrimitive(LangParser::TypePrimitiveContext *ctx) override;
  std::any visitTypeListType(LangParser::TypeListTypeContext *ctx) override;
  std::any visitTypeMap(LangParser::TypeMapContext *ctx) override;
  std::any visitTypeAny(LangParser::TypeAnyContext *ctx) override;
  std::any visitPrimitiveType(LangParser::PrimitiveTypeContext *ctx) override;
  std::any visitListType(LangParser::ListTypeContext *ctx) override;
  std::any visitMapType(LangParser::MapTypeContext *ctx) override;

  // 表达式
  std::any visitExpression(LangParser::ExpressionContext *ctx) override;
  std::any visitLogicalOrExpression(LangParser::LogicalOrExpressionContext *ctx) override;
  std::any visitLogicalAndExpression(LangParser::LogicalAndExpressionContext *ctx) override;
  std::any visitBitwiseOrExpression(LangParser::BitwiseOrExpressionContext *ctx) override;
  std::any visitBitwiseXorExpression(LangParser::BitwiseXorExpressionContext *ctx) override;
  std::any visitBitwiseAndExpression(LangParser::BitwiseAndExpressionContext *ctx) override;
  std::any visitEqualityExpression(LangParser::EqualityExpressionContext *ctx) override;
  std::any visitComparisonExpression(LangParser::ComparisonExpressionContext *ctx) override;
  std::any visitShiftExpression(LangParser::ShiftExpressionContext *ctx) override;
  std::any visitConcatExpression(LangParser::ConcatExpressionContext *ctx) override;
  std::any visitAddSubExpression(LangParser::AddSubExpressionContext *ctx) override;
  std::any visitMulDivModExpression(LangParser::MulDivModExpressionContext *ctx) override;
  std::any visitUnaryPrefix(LangParser::UnaryPrefixContext *ctx) override;
  std::any visitUnaryToPostfix(LangParser::UnaryToPostfixContext *ctx) override;
  std::any visitPostfixExpression(LangParser::PostfixExpressionContext *ctx) override;

  // Primary 表达式
  std::any visitPrimaryAtom(LangParser::PrimaryAtomContext *ctx) override;
  std::any visitPrimaryListLiteral(LangParser::PrimaryListLiteralContext *ctx) override;
  std::any visitPrimaryMapLiteral(LangParser::PrimaryMapLiteralContext *ctx) override;
  std::any visitPrimaryIdentifier(LangParser::PrimaryIdentifierContext *ctx) override;
  std::any visitPrimaryVarArgs(LangParser::PrimaryVarArgsContext *ctx) override;
  std::any visitPrimaryParenExp(LangParser::PrimaryParenExpContext *ctx) override;
  std::any visitPrimaryNew(LangParser::PrimaryNewContext *ctx) override;
  std::any visitPrimaryLambda(LangParser::PrimaryLambdaContext *ctx) override;

  // 原子 & 字面量
  std::any visitAtomexp(LangParser::AtomexpContext *ctx) override;
  std::any visitLambdaExprDef(LangParser::LambdaExprDefContext *ctx) override;
  std::any visitListLiteralDef(LangParser::ListLiteralDefContext *ctx) override;
  std::any visitMapLiteralDef(LangParser::MapLiteralDefContext *ctx) override;
  std::any visitMapEntryIdentKey(LangParser::MapEntryIdentKeyContext *ctx) override;
  std::any visitMapEntryExprKey(LangParser::MapEntryExprKeyContext *ctx) override;
  std::any visitMapEntryStringKey(LangParser::MapEntryStringKeyContext *ctx) override;

  // 其他
  std::any visitNewExpressionDef(LangParser::NewExpressionDefContext *ctx) override;
  std::any visitIfStatement(LangParser::IfStatementContext *ctx) override;
  std::any visitWhileStatement(LangParser::WhileStatementContext *ctx) override;
  std::any visitForStatement(LangParser::ForStatementContext *ctx) override;
  std::any visitForUpdate(LangParser::ForUpdateContext *ctx) override;
  std::any visitForUpdateSingle(LangParser::ForUpdateSingleContext *ctx) override;
  std::any visitForCStyleControl(LangParser::ForCStyleControlContext *ctx) override;
  std::any visitForEachExplicitControl(LangParser::ForEachExplicitControlContext *ctx) override;
  std::any visitForInitStatement(LangParser::ForInitStatementContext *ctx) override;
  std::any visitMultiDeclaration(LangParser::MultiDeclarationContext *ctx) override;
  std::any visitQualifiedIdentifier(LangParser::QualifiedIdentifierContext *ctx) override;

  std::any visitMultiReturnFunctionDeclarationDef(
      LangParser::MultiReturnFunctionDeclarationDefContext *ctx) override;
  std::any visitMultiReturnClassMethodMember(
      LangParser::MultiReturnClassMethodMemberContext *context) override;

  std::any visitImportStmt(LangParser::ImportStmtContext *ctx) override;
  std::any visitImportNamespaceStmt(LangParser::ImportNamespaceStmtContext *ctx) override;
  std::any visitImportNamedStmt(LangParser::ImportNamedStmtContext *ctx) override;
  std::any visitImportSpecifier(LangParser::ImportSpecifierContext *ctx) override;
  std::any visitArguments(LangParser::ArgumentsContext *ctx) override;
  std::any visitExpressionList(LangParser::ExpressionListContext *ctx) override;
  std::any
  visitTypeQualifiedIdentifier(LangParser::TypeQualifiedIdentifierContext *context) override;

  // 非重载函数
  std::any visitSuffix(LangParser::PostfixSuffixContext *suffixNode, const TypeInfoPtr &baseType);
  std::any visitPostfixIndexSuffix(LangParser::PostfixIndexSuffixContext *ctx,
                                   const TypeInfoPtr &baseType);
  std::any visitPostfixMemberSuffix(LangParser::PostfixMemberSuffixContext *ctx,
                                    const TypeInfoPtr &baseType);
  std::any visitPostfixMemberSuffix_like(antlr4::tree::TerminalNode *identifierNode,
                                         const TypeInfoPtr &baseType,
                                         antlr4::ParserRuleContext *errorNode);
  std::any visitPostfixCallSuffix(LangParser::PostfixCallSuffixContext *ctx,
                                  const TypeInfoPtr &baseType);

  std::any visitLvalueSuffix(LangParser::LvalueSuffixContext *suffixNode,
                             const TypeInfoPtr &baseType);
  TypeInfoPtr inferBinaryOperationType(const TypeInfoPtr &leftType, const std::string &op,
                                       const TypeInfoPtr &rightType,
                                       antlr4::ParserRuleContext *ctx);
};

#endif // SEMANTIC_ANALYZER_VISITOR_H