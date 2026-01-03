#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include "antlr4-runtime.h" // 引入 ANTLR4 运行时核心头文件
#include "protocol_types.h" // 引入 LSP 协议类型定义 (包含 Diagnostic, Range, Position 等)
#include <cmath>            // 引入数学库 (用于 max)
#include <exception>        // 引入 C++ 标准异常库 (用于 std::exception_ptr)
#include <string>           // 引入 C++ 标准字符串库
#include <vector>           // 引入 C++ 标准向量容器

// 自定义 ANTLR 错误监听器，用于收集 LSP 格式的诊断信息
class DiagnosticErrorListener : public antlr4::BaseErrorListener {
private:
  std::vector<Diagnostic> &diagnostics; // 引用外部存储诊断信息的向量

  // 辅助函数：将 ANTLR 的行/列/长度信息转换为 LSP 的 Range
  // ANTLR: line 是 1-based, charPositionInLine 是 0-based
  // LSP:   line 是 0-based, character 是 0-based
  Range tokenToRange(antlr4::Token *token) {
    if (!token) {
      // 如果没有具体的 token (例如 EOF 错误)，返回一个默认的单点范围
      return Range{Position{0, 0}, Position{0, 1}};
    }
    long tokenLine = static_cast<long>(token->getLine());
    long tokenStartChar = static_cast<long>(token->getCharPositionInLine());
    long tokenStopChar =
        tokenStartChar + static_cast<long>(token->getStopIndex() - token->getStartIndex() + 1);

    // 转换为 LSP 的 0-based 格式
    long lspLine = tokenLine - 1;
    long lspStartChar = tokenStartChar;
    long lspEndChar = tokenStopChar;

    // 基本的健全性检查，确保 line >= 0, char >= 0
    lspLine = std::max(0L, lspLine);
    lspStartChar = std::max(0L, lspStartChar);
    lspEndChar = std::max(lspStartChar, lspEndChar); // 确保 end 不在 start 之前

    return Range{Position{lspLine, lspStartChar}, Position{lspLine, lspEndChar}};
  }

  Range positionToRange(size_t line, size_t charPositionInLine, size_t length) {
    long lspLine = static_cast<long>(line) - 1; // ANTLR line is 1-based -> LSP line is 0-based
    long lspCharStart =
        static_cast<long>(charPositionInLine); // ANTLR charPos is 0-based, LSP char is 0-based
    long lspCharEnd = lspCharStart + static_cast<long>(length);

    lspLine = std::max(0L, lspLine);
    lspCharStart = std::max(0L, lspCharStart);
    lspCharEnd = std::max(lspCharStart, lspCharEnd);

    return Range{Position{lspLine, lspCharStart}, Position{lspLine, lspCharEnd}};
  }

public:
  // 构造函数，接收一个用于存储诊断信息的向量的引用
  DiagnosticErrorListener(std::vector<Diagnostic> &diags) : diagnostics(diags) {}

  // 覆盖 ANTLR 的 syntaxError 方法
  // 当解析器遇到语法错误时，此方法会被调用
  void syntaxError(antlr4::Recognizer *recognizer, // 触发错误的识别器 (Lexer 或 Parser)
                   antlr4::Token *offendingSymbol, // 引起错误的 Token (可能为 nullptr)
                   size_t line,                    // 错误发生的行号 (1-based)
                   size_t charPositionInLine,      // 错误发生的列号 (0-based)
                   const std::string &msg,         // ANTLR 生成的错误消息
                   std::exception_ptr e) override  // 相关的异常信息 (可能为 nullptr)
  {
    Diagnostic diag;

    // 确定错误范围
    size_t length = 1; // 默认长度为 1 个字符
    if (offendingSymbol != nullptr) {
      // 如果有 offendingSymbol，使用它的长度
      length = offendingSymbol->getStopIndex() - offendingSymbol->getStartIndex() + 1;
      // 使用 offendingSymbol 的精确位置计算 Range
      diag.range = tokenToRange(offendingSymbol);
    } else {
      // 如果没有 offendingSymbol (例如 EOF 错误)，使用 ANTLR 报告的 line/charPosition
      diag.range = positionToRange(line, charPositionInLine, length);
    }

    diag.severity = DiagnosticSeverity::Error; // 将语法错误标记为 Error 级别
    diag.message = msg;                        // 使用 ANTLR 的错误消息
    diag.source = "syntax";                    // (可选) 标记来源为 "syntax"

    diagnostics.push_back(diag); // 将构建好的诊断信息添加到向量中
  }

  // 可以选择性地覆盖其他报告方法，例如：
  // void reportAmbiguity(...) override { /* 处理二义性 */ }
  // void reportAttemptingFullContext(...) override { /* 处理 SLL/LL(*) 冲突 */ }
  // void reportContextSensitivity(...) override { /* 处理上下文敏感性 */ }
  // 这些通常可以报告为警告或提示，或者仅用于调试。
};

#endif // DIAGNOSTICS_H