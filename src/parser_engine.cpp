#include "parser_engine.h"  // 包含 ParseResult 和 ParserEngine 的声明
#include "LangLexer.h"      // 包含 ANTLR 生成的 Lexer 类 (需要 ANTLR 生成)
#include "LangParser.h"     // 包含 ANTLR 生成的 Parser 类 (需要 ANTLR 生成)
#include "antlr4-runtime.h" // 引入 ANTLR 核心运行时库
#include "diagnostics.h"    // 包含 DiagnosticErrorListener 的声明
#include "semantic_analyzer_visitor.h"
#include "sptscript_analyzer.h"

#include <exception> // 引入标准异常库
#include <memory>
#include <memory>    // 引入标准内存库 (用于 unique_ptr)
#include <stdexcept> // 用于 std::runtime_error (如果需要)
#include <string>    // 引入标准字符串库
#include <vector>    // 引入标准向量库

// --- 引入 plog 日志库 ---
#include <plog/Log.h>

/**
 * @brief 对给定的源代码字符串执行词法和语法分析。
 *
 * 此函数封装了 ANTLR 的标准解析流程：
 * 1. 创建输入流。
 * 2. 创建词法分析器 (Lexer)。
 * 3. 创建词法符号流 (Token Stream)。
 * 4. 创建语法分析器 (Parser)。
 * 5. 设置自定义错误监听器以收集诊断信息。
 * 6. 调用解析器的入口规则开始解析，生成具体语法树 (CST)。
 * 7. 将 CST、收集到的诊断信息和 Token 流打包到 ParseResult 中返回。
 *
 * @param sourceCode 要解析的源代码文本。
 * @param sourceName 源文件的标识符（例如文件名或 URI），主要用于诊断信息中的位置标识。
 * @return ParseResult 结构，包含 CST 的根节点、语法诊断列表和 Token 流。
 * 如果发生无法恢复的内部解析错误，cstRoot 可能为 nullptr。
 */
ParseResultPtr ParserEngine::parse(const std::string &sourceCode, const std::string &sourceName) {
  PLOGD << "开始解析源: " << sourceName << " (大小: " << sourceCode.length() << " 字节)";

  ParseResultPtr result = std::make_shared<ParseResult>(); // 初始化用于存储结果的对象

  // 1. 创建 ANTLR 输入流 (从字符串)
  auto inputStream = new antlr4::ANTLRInputStream(sourceCode);
  inputStream->name = sourceName; // 为输入流命名，有助于错误报告

  // 2. 创建词法分析器 (Lexer)
  result->lexer = new LangLexer(inputStream);

  // --- 错误监听器设置 ---
  // 移除 Lexer 默认的 ConsoleErrorListener，避免它向 stderr 打印错误
  result->lexer->removeErrorListeners();
  // 注意：通常语法错误在 Parser 阶段捕获更全面。如果需要捕获词法错误（例如未识别的字符），
  // 可以取消注释下面两行，让 Lexer 也使用我们的诊断收集器。
  // DiagnosticErrorListener lexerErrorListener(result->syntaxDiagnostics);
  // lexer.addErrorListener(&lexerErrorListener);

  // 3. 创建词法符号流 (Token Stream)
  // 使用 shared_ptr 来管理 tokenStream 的生命周期，并将其存储在 result 中
  result->tokenStream = new antlr4::CommonTokenStream(result->lexer);

  PLOGV << "词法分析完成，Token 流已创建。";

  // 4. 创建语法分析器 (Parser)
  // Parser 需要 Token 流的原始指针
  result->parser = new LangParser(result->tokenStream);
  // --- 错误处理设置 ---
  // 移除 Parser 默认的 ConsoleErrorListener
  result->parser->removeErrorListeners();
  // 创建并添加我们自定义的 DiagnosticErrorListener 实例
  // 将 result->syntaxDiagnostics 向量的引用传递给监听器，以便它能收集错误

  DiagnosticErrorListener parserErrorListener = DiagnosticErrorListener(result->syntaxDiagnostics);
  result->parser->addErrorListener(&parserErrorListener);

  // 可选: 配置 Parser 的错误处理策略
  // parser.setErrorHandler(std::make_shared<antlr4::BailErrorStrategy>()); //
  // 遇到第一个错误就抛出异常并停止 默认的 DefaultErrorStrategy 包含恢复机制，通常更适合 LSP
  // parser.setErrorHandler(std::make_shared<antlr4::DiagnosticErrorStrategy>()); //
  // 尝试提供更详细的诊断

  // --- 执行语法分析 ---
  try {
    PLOGD << "开始语法分析...";
    // 调用语法的入口规则 (根据你的 .g4 文件确定，通常是第一个规则)
    // 这将触发解析过程，并通过监听器报告语法错误
    result->cstRoot = result->parser->compilationUnit();

    PLOGD << "语法分析完成。CST 根节点: " << (result->cstRoot ? "已生成" : "未生成(nullptr)");

    // 检查并记录收集到的语法错误数量
    if (!result->syntaxDiagnostics.empty()) {
      PLOGW << "解析源 '" << sourceName << "' 时发现 " << result->syntaxDiagnostics.size()
            << " 个语法错误。";
    } else {
      PLOGI << "解析源 '" << sourceName << "' 未发现语法错误。";
    }

  } catch (const antlr4::ParseCancellationException &e) {
    // 如果使用了 BailErrorStrategy，错误会通过此异常抛出
    PLOGE << "语法分析被取消 (可能由于 BailErrorStrategy 或严重错误): " << e.what();
    // 添加一条通用诊断信息
    Diagnostic diag;
    diag.message = "解析被取消，可能存在严重语法错误: " + std::string(e.what());
    diag.range = Range{Position{0, 0}, Position{0, 1}}; // 指向文档开头
    diag.severity = DiagnosticSeverity::Error;
    diag.source = "parser";
    result->syntaxDiagnostics.push_back(std::move(diag));
    result->cstRoot = nullptr; // 确保 CST 无效

  } catch (const std::exception &e) {
    // 捕获 ANTLR 运行时或其他标准库可能抛出的异常
    PLOGF << "在 ANTLR 解析过程中发生严重异常: " << e.what();
    Diagnostic diag;
    diag.message = "内部解析器错误: " + std::string(e.what());
    diag.range = Range{Position{0, 0}, Position{0, 1}};
    diag.severity = DiagnosticSeverity::Error;
    diag.source = "parser";
    result->syntaxDiagnostics.push_back(std::move(diag));
    result->cstRoot = nullptr;
  } catch (...) {
    // 捕获所有其他未知类型的异常
    PLOGF << "在 ANTLR 解析过程中发生未知类型的严重异常。";
    Diagnostic diag;
    diag.message = "未知的内部解析器错误";
    diag.range = Range{Position{0, 0}, Position{0, 1}};
    diag.severity = DiagnosticSeverity::Error;
    diag.source = "parser";
    result->syntaxDiagnostics.push_back(std::move(diag));
    result->cstRoot = nullptr;
  }

  PLOGD << "解析引擎完成处理: " << sourceName;
  // 返回包含 CST、诊断信息和 Token 流的结果对象
  return result;
}

ParseResult::~ParseResult() {
  delete parser;
  delete lexer;
  delete tokenStream;
}
