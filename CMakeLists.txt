cmake_minimum_required(VERSION 3.15)
project(sptScriptLsp LANGUAGES CXX C)
# --- 设置 C 标准 ---
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_C_EXTENSIONS OFF)

# --- 设置 C++ 标准 ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    add_compile_options("/source-charset:utf-8")
endif()
if (MINGW)
    add_compile_options(-fexec-charset=GBK -finput-charset=UTF-8)
endif ()
include_directories(${PROJECT_SOURCE_DIR}/external)
include_directories(${PROJECT_SOURCE_DIR}/external/nlohmann)
include_directories(${PROJECT_SOURCE_DIR}/include)


add_subdirectory(runtime)

set(ANTLR_GENERATED_DIR ${PROJECT_SOURCE_DIR}/generated)

set(LSP_SERVER_SOURCES
    src/main.cpp
    ${ANTLR_GENERATED_DIR}/LangLexer.cpp
    ${ANTLR_GENERATED_DIR}/LangParser.cpp
    ${ANTLR_GENERATED_DIR}/LangParserBaseVisitor.cpp
        src/document_manager.cpp
        src/parser_engine.cpp
        src/semantic_info.cpp
        src/symbol_table.cpp
        src/sptscript_analysis.cpp
        src/lsp_server.cpp
)

# --- 定义可执行目标 ---
add_executable(sptscript-lsp ${LSP_SERVER_SOURCES})
target_include_directories(sptscript-lsp PRIVATE ${PROJECT_SOURCE_DIR}/generated)
target_include_directories(sptscript-lsp PRIVATE ${PROJECT_SOURCE_DIR}/runtime/src)

# --- 链接依赖库 ---
target_link_libraries(sptscript-lsp PRIVATE antlr4_static)
target_link_options(sptscript-lsp PRIVATE "-static-libgcc" "-static-libstdc++" "-static")
