// ast.cpp
// 轻量级 C++ 结构 AST (面向格式化)
// 用法:
//   ./ast filename            打印 AST 树并做 round-trip / 括号配平校验
//   ./ast filename --check    仅做校验, 不打印树
//
// 设计目标:
//   1. 复用 codeformat.cpp 的 tokenizer (通过 #include + 宏屏蔽 main).
//   2. 生成结构树:
//        - 匹配的 () [] {} 分别归为 PAREN / BRACKET / BLOCK 节点 (递归);
//        - if/for/while/switch/catch (含可选 else) 归为 CONTROL 节点
//          (关键字 + 条件 PAREN + 体);
//        - 预处理行 / 注释 作为独立叶子;
//        - 其余扁平 token 序列 (含空白) 归为 RUN 节点.
//   3. Round-trip 不变量: 中序拼接所有叶子 == 原始源码 (不增删一字符).
//      这是格式化器 AST 的根本保证 -- 结构化只重组关系, 不动内容.
//   4. 不做完整 C++ 语义分析, 仅提供格式化所需的上下文结构.
//
// 验证策略:
//   (1) round-trip: emit(tree) == 原文.
//   (2) 括号配平: token 流中 ()/[]/{} 计数归零.
//   (3) 幂等性: parse(emit(tree)) 再 emit 应与 emit(tree) 相同 (结构稳定).

#define main codeformat_main
#include "codeformat.cpp"
#undef main

#include <iostream>
#include <string>
#include <vector>

// ---------- AST 节点 ----------
struct AstNode {
    enum Kind {
        K_TU, K_BLOCK, K_PAREN, K_BRACKET, K_CONTROL, K_RUN,
        K_TOKEN, K_WS, K_COMMENT, K_PREPROC
    };
    Kind kind = K_TOKEN;
    std::string text;                 // 叶子文本
    std::vector<AstNode> children;    // 组节点子节点
    int tokStart = -1, tokEnd = -1;   // token 流下标区间 [start, end)

    bool isLeaf() const {
        return kind == K_TOKEN || kind == K_WS || kind == K_COMMENT || kind == K_PREPROC;
    }
    const char* kindName() const {
        switch (kind) {
            case K_TU:      return "TU";
            case K_BLOCK:   return "BLOCK";
            case K_PAREN:   return "PAREN";
            case K_BRACKET: return "BRACKET";
            case K_CONTROL: return "CONTROL";
            case K_RUN:     return "RUN";
            case K_TOKEN:   return "TOKEN";
            case K_WS:      return "WS";
            case K_COMMENT: return "COMMENT";
            case K_PREPROC: return "PREPROC";
        }
        return "?";
    }
};

// ---------- 解析器 ----------
struct AstParser {
    const std::vector<Token>& toks;
    int pos = 0;
    int n;
    explicit AstParser(const std::vector<Token>& t) : toks(t), n((int)t.size()) {}

    static bool isControlKw(const std::string& s) {
        return s == "if" || s == "for" || s == "while" || s == "switch" || s == "catch";
    }
    // p 处是否为 "kw (" 形态的控制结构起点
    bool isControlAt(int p) const {
        if (p < 0 || p >= n) return false;
        if (toks[p].type != 0 || !isControlKw(toks[p].text)) return false;
        for (int k = p + 1; k < n; k++) {
            if (toks[k].type != 1)
                return toks[k].type == 0 && toks[k].text == "(";
        }
        return false;
    }

    AstNode leaf(AstNode::Kind k, int p) {
        AstNode node;
        node.kind = k;
        node.text = toks[p].text;
        node.tokStart = p;
        node.tokEnd = p + 1;
        return node;
    }

    // 在当前作用域内解析成员序列.
    // 任何 ) } ] 都会终止扫描 (闭合符交由调用者消费), 保证不会"偷走"外层括号.
    // stopAtSemi=true 时, 消费掉顶层第一个 ';' 后停止 (用于控制结构体).
    std::vector<AstNode> parseMembers(bool stopAtSemi) {
        std::vector<AstNode> members;
        AstNode run; run.kind = AstNode::K_RUN; run.tokStart = pos; bool act = false;
        auto flush = [&]() {
            if (act) {
                run.tokEnd = pos;
                members.push_back(std::move(run));
                run = AstNode{};
                run.kind = AstNode::K_RUN;
                run.tokStart = pos;
                act = false;
            }
        };
        while (pos < n) {
            const Token& tk = toks[pos];
            if (tk.type == 1) { // 空白
                if (!act) { run.tokStart = pos; act = true; }
                run.children.push_back(leaf(AstNode::K_WS, pos));
                pos++;
                continue;
            }
            if (tk.type == 3) { // 注释
                flush();
                members.push_back(leaf(AstNode::K_COMMENT, pos));
                pos++;
                continue;
            }
            if (tk.type == 4) { // 预处理行
                flush();
                members.push_back(leaf(AstNode::K_PREPROC, pos));
                pos++;
                continue;
            }
            if (tk.type == 0) {
                if (tk.text == "{") { flush(); members.push_back(parseBlock()); continue; }
                if (tk.text == "(") { flush(); members.push_back(parseParen()); continue; }
                if (tk.text == "[") { flush(); members.push_back(parseBracket()); continue; }
                if (tk.text == ")" || tk.text == "}" || tk.text == "]") break; // 闭合符
                if (isControlAt(pos)) { flush(); members.push_back(parseControl()); continue; }
            }
            // 普通代码 token (type==0 非结构) 或字符串字面量 (type==2)
            if (!act) { run.tokStart = pos; act = true; }
            run.children.push_back(leaf(AstNode::K_TOKEN, pos));
            pos++;
            if (stopAtSemi && tk.type == 0 && tk.text == ";") break;
        }
        flush();
        return members;
    }

    AstNode parseBlock() {
        AstNode node; node.kind = AstNode::K_BLOCK; node.tokStart = pos;
        node.children.push_back(leaf(AstNode::K_TOKEN, pos)); pos++; // '{'
        auto inner = parseMembers(false);
        for (auto& c : inner) node.children.push_back(std::move(c));
        if (pos < n && toks[pos].type == 0 && toks[pos].text == "}") {
            node.children.push_back(leaf(AstNode::K_TOKEN, pos)); pos++;
        }
        node.tokEnd = pos;
        return node;
    }
    AstNode parseParen() {
        AstNode node; node.kind = AstNode::K_PAREN; node.tokStart = pos;
        node.children.push_back(leaf(AstNode::K_TOKEN, pos)); pos++; // '('
        auto inner = parseMembers(false);
        for (auto& c : inner) node.children.push_back(std::move(c));
        if (pos < n && toks[pos].type == 0 && toks[pos].text == ")") {
            node.children.push_back(leaf(AstNode::K_TOKEN, pos)); pos++;
        }
        node.tokEnd = pos;
        return node;
    }
    AstNode parseBracket() {
        AstNode node; node.kind = AstNode::K_BRACKET; node.tokStart = pos;
        node.children.push_back(leaf(AstNode::K_TOKEN, pos)); pos++; // '['
        auto inner = parseMembers(false);
        for (auto& c : inner) node.children.push_back(std::move(c));
        if (pos < n && toks[pos].type == 0 && toks[pos].text == "]") {
            node.children.push_back(leaf(AstNode::K_TOKEN, pos)); pos++;
        }
        node.tokEnd = pos;
        return node;
    }

    // 跳过一段空白, 将其作为 WS 叶子追加到 out
    void consumeWs(std::vector<AstNode>& out) {
        while (pos < n && toks[pos].type == 1) {
            out.push_back(leaf(AstNode::K_WS, pos));
            pos++;
        }
    }

    // 解析控制结构体: BLOCK | 嵌套 CONTROL | RUN(到 ';' 含)
    void parseBody(std::vector<AstNode>& out) {
        consumeWs(out);
        if (pos < n && toks[pos].type == 0 && toks[pos].text == "{") {
            out.push_back(parseBlock());
        } else if (isControlAt(pos)) {
            out.push_back(parseControl());
        } else {
            auto body = parseMembers(true);
            for (auto& c : body) out.push_back(std::move(c));
        }
    }

    AstNode parseControl() {
        AstNode node; node.kind = AstNode::K_CONTROL; node.tokStart = pos;
        // 关键字
        node.children.push_back(leaf(AstNode::K_TOKEN, pos)); pos++;
        consumeWs(node.children);
        // 条件 PAREN
        if (pos < n && toks[pos].type == 0 && toks[pos].text == "(") {
            node.children.push_back(parseParen());
        }
        // 体
        parseBody(node.children);

        // else 分支 (仅紧跟在 if 体后时链接进来)
        // 探测时不加叶子, 命中 else 再补, 避免 "未命中却已输出 ws" 导致重复.
        int save = pos;
        while (pos < n && toks[pos].type == 1) pos++; // 探测性跳过 ws
        if (pos < n && toks[pos].type == 0 && toks[pos].text == "else") {
            for (int k = save; k < pos; k++) node.children.push_back(leaf(AstNode::K_WS, k));
            node.children.push_back(leaf(AstNode::K_TOKEN, pos)); pos++; // else
            parseBody(node.children);
        } else {
            pos = save; // 不是 else, ws 留给外层, 不加叶子
        }
        node.tokEnd = pos;
        return node;
    }

    AstNode parseTu() {
        AstNode node; node.kind = AstNode::K_TU; node.tokStart = 0;
        node.children = parseMembers(false);
        node.tokEnd = n;
        return node;
    }
};

// ---------- 输出 / 校验 ----------
// 中序拼接所有叶子 -> 文本 (round-trip)
static void emitLeaves(const AstNode& node, std::string& out) {
    if (node.isLeaf()) { out += node.text; return; }
    for (const auto& c : node.children) emitLeaves(c, out);
}

static bool checkBalance(const std::vector<Token>& toks) {
    long p = 0, b = 0, br = 0;
    for (const auto& tk : toks) {
        if (tk.type != 0) continue;
        if (tk.text == "(") p++;
        else if (tk.text == ")") p--;
        else if (tk.text == "{") b++;
        else if (tk.text == "}") b--;
        else if (tk.text == "[") br++;
        else if (tk.text == "]") br--;
    }
    return p == 0 && b == 0 && br == 0;
}

static std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '\n': o += "\\n"; break;
            case '\t': o += "\\t"; break;
            case '\r': o += "\\r"; break;
            case '\\': o += "\\\\"; break;
            case '"':  o += "\\\""; break;
            default:   o += c;
        }
    }
    return o;
}

static void dump(const AstNode& node, int depth, std::ostream& os) {
    for (int i = 0; i < depth; i++) os << "  ";
    os << node.kindName();
    if (node.isLeaf()) {
        os << " \"" << esc(node.text) << "\"";
    } else {
        os << "  <" << node.tokStart << ".." << node.tokEnd << ">  children=" << node.children.size();
    }
    os << "\n";
    for (const auto& c : node.children) dump(c, depth + 1, os);
}

int main(int argc, char** argv) {
    bool checkOnly = false;
    std::string filename;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--check") checkOnly = true;
        else if (a == "--help" || a == "-h") {
            std::cout << "Usage: " << argv[0] << " filename [--check]\n";
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "ast: unknown option: " << a << "\n";
            return 1;
        } else {
            filename = a;
        }
    }
    if (filename.empty()) {
        std::cerr << "Usage: " << argv[0] << " filename [--check]\n";
        return 1;
    }

    std::string src;
    if (!read_file(filename, src)) {
        std::cerr << "ast: cannot read " << filename << "\n";
        return 1;
    }

    std::vector<Token> toks = tokenize(src);
    AstParser parser(toks);
    AstNode root = parser.parseTu();

    // (1) round-trip
    std::string rt;
    emitLeaves(root, rt);
    bool roundtripOk = (rt == src);

    // (2) 括号配平
    bool balanceOk = checkBalance(toks);

    // (3) 幂等: 对 emit 结果再 parse 再 emit, 应相同
    std::vector<Token> toks2 = tokenize(rt);
    AstParser parser2(toks2);
    AstNode root2 = parser2.parseTu();
    std::string rt2;
    emitLeaves(root2, rt2);
    bool idempotentOk = (rt2 == rt);

    if (checkOnly) {
        std::cout << "[ " << filename << " ]\n";
        std::cout << "tokens        : " << toks.size() << "\n";
        std::cout << "round-trip    : " << (roundtripOk ? "OK" : "FAIL") << "\n";
        std::cout << "brace balance : " << (balanceOk ? "OK" : "FAIL") << "\n";
        std::cout << "idempotent    : " << (idempotentOk ? "OK" : "FAIL") << "\n";
        if (!roundtripOk) {
            size_t p = 0;
            while (p < rt.size() && p < src.size() && rt[p] == src[p]) p++;
            std::cout << "  round-trip first diff at offset " << p
                      << " (orig=" << src.size() << ", ast=" << rt.size() << ")\n";
        }
        return (roundtripOk && balanceOk && idempotentOk) ? 0 : 1;
    }

    dump(root, 0, std::cout);
    std::cout << std::string(50, '=') << "\n";
    std::cout << "tokens        : " << toks.size() << "\n";
    std::cout << "round-trip    : " << (roundtripOk ? "OK" : "FAIL") << "\n";
    std::cout << "brace balance : " << (balanceOk ? "OK" : "FAIL") << "\n";
    std::cout << "idempotent    : " << (idempotentOk ? "OK" : "FAIL") << "\n";
    return (roundtripOk && balanceOk && idempotentOk) ? 0 : 1;
}
