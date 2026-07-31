现在有三个部分:
1. codeformat.cpp  格式化主体 (基于 token 流)
2. sample.cpp      测试样例
3. ast.cpp         轻量级结构 AST 解析器 (复用 tokenizer, 生成 BLOCK/PAREN/BRACKET/CONTROL 等结构树, 保证 round-trip 不变量)

验证工具: verify.cpp (token 一致性 + 幂等性), ast.cpp --check (round-trip + 括号配平 + 幂等).
想到更多的继续添加.
