#ifndef PARSER_ENGINE_H
#define PARSER_ENGINE_H

#include "antlr4-runtime.h" // 引入 ANTLR4 运行时核心头文件
#include "protocol_types.h" // 引入 LSP 协议类型定义 (需要 Diagnostic)
#include <memory>           // 引入 C++ 标准内存管理库 (用于 unique_ptr)
#include <string>           // 引入 C++ 标准字符串库
#include <vector>           // 引入 C++ 标准向量容器

// 前置声明 ANTLR 生成的类，以减少头文件依赖
// 实际的 #include 将放在对应的 .cpp 文件中
class LangLexer;
class LangParser;

// antlr4::tree::ParseTree 定义在 antlr4-runtime.h 中
// antlr4::CommonTokenStream 定义在 antlr4-runtime.h 中

/**
 * @brief 定义解析操作的结果结构体。
 * 这个结构体包含了语法分析的主要产物。
 */
struct ParseResult {
  /**
   * @brief 指向具体语法树 (CST) 根节点的指针。
   * CST 是源代码结构的直接表示，包含了所有词法和语法信息。
   * 注意：此指针通常不拥有内存，其生命周期依赖于 ANTLR 内部结构。
   * 建议与拥有 tokenStream 的 unique_ptr 关联生命周期。
   */
  antlr4::tree::ParseTree *cstRoot = nullptr;
  LangLexer *lexer = nullptr;
  LangParser *parser = nullptr;
  /**
   * @brief 在词法和语法分析阶段收集到的所有语法诊断信息。
   */
  std::vector<Diagnostic> syntaxDiagnostics;

  /**
   * @brief 指向包含所有词法符号 (Token) 的流的独占指针 (owning pointer)。
   * 保存这个流对于后续需要访问具体 Token 信息（如注释、空格、原始文本）的分析很有用。
   * 使用 unique_ptr 确保 ParseResult 销毁时 Token 流也被正确释放。
   */
  antlr4::CommonTokenStream *tokenStream = nullptr;

  // 默认构造函数
  ParseResult() = default;
  //    ParseResult(antlr4::tree::ParseTree *cstRoot, LangLexer* lexer, LangParser* parser);
  ~ParseResult();
  // 删除拷贝构造函数和拷贝赋值运算符，因为 unique_ptr 不应被拷贝
  ParseResult(const ParseResult &) = delete;
  ParseResult &operator=(const ParseResult &) = delete;
};

using ParseResultPtr = std::shared_ptr<ParseResult>;

/**
 * @brief 解析器引擎类。
 * 提供一个静态接口来封装调用 ANTLR 进行词法和语法分析的过程。
 */
class ParserEngine {
public:
  /**
   * @brief 对给定的 SptScript 源代码字符串进行词法和语法分析。
   * @param sourceCode 要解析的源代码文本。
   * @param sourceName 源文件的标识符（例如文件名或 URI 字符串），用于诊断信息中的位置标识。
   * @return ParseResult 结构，包含分析结果：CST 根节点、语法诊断列表和 Token 流。
   * 如果发生无法恢复的内部解析错误，cstRoot 可能为 nullptr。
   */
  static ParseResultPtr parse(const std::string &sourceCode,
                              const std::string &sourceName = "<input>");

  // 这个类目前只包含静态方法，因为解析过程本身设计为无状态。
};

#endif // PARSER_ENGINE_H