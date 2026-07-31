// codeformat.cpp
// 整理 C++ 代码格式
// 用法: ./codeformat filename
// 行为: 生成同名新文件 (内容为格式化结果), 旧文件备份为 filename.bak
//
// 规范:
//   1. 引用/指针靠近变量名: int* A -> int *A ; int& A -> int &A
//   2. 逗号后不加空格: func(int a,int b)
//   3. for 内分号后不加空格: for (int i = 0;i < 10;++i)
//   4. 短函数体大括号内部首尾空格去掉: { return true; } -> {return true;}
//   5. if/for/while 括号内部首尾空格去掉: for ( int i=0 ) -> for (int i=0)
//   6. if/for/while 与括号间保留一个空格: if (true)
//
// 验证策略:
//   (1) token 序列一致性: 原文与格式化结果剔除空白 token 后必须完全相同,
//       保证格式化只改空白分布, 不增删/重排任何有意义的 token (语义不变).
//   (2) 幂等性: 再格式化一次结果应与一次格式化完全相同 (不动点).
//   (3) 终极保证: 原文件与格式化后文件分别编译并运行, 行为应一致.

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <cstring>
#include <cctype>

// ---------- Tokenizer ----------
struct Token {
    std::string text;
    int type; // 0=代码 1=空白 2=字符串 3=注释 4=预处理行
};

static bool is_idstart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
static bool is_idchar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

static bool is_rawstr_prefix(const std::string& id) {
    if (id == "R") return true;
    if (id.size() < 2) return false;
    if (id.back() != 'R') return false;
    std::string pre = id.substr(0, id.size() - 1);
    return pre == "u" || pre == "U" || pre == "L" || pre == "u8";
}

static std::vector<Token> tokenize(const std::string& src) {
    std::vector<Token> out;
    size_t i = 0, n = src.size();

    while (i < n) {
        char c = src[i];

        // 空白
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
            size_t j = i;
            while (j < n) {
                char d = src[j];
                if (d == ' ' || d == '\t' || d == '\n' || d == '\r' || d == '\f' || d == '\v') j++;
                else break;
            }
            out.push_back({src.substr(i, j - i), 1});
            i = j;
            continue;
        }

        // 行注释
        if (c == '/' && i + 1 < n && src[i + 1] == '/') {
            size_t j = i;
            while (j < n && src[j] != '\n') j++;
            out.push_back({src.substr(i, j - i), 3});
            i = j;
            continue;
        }
        // 块注释
        if (c == '/' && i + 1 < n && src[i + 1] == '*') {
            size_t j = i + 2;
            while (j < n) {
                if (src[j] == '*' && j + 1 < n && src[j + 1] == '/') { j += 2; break; }
                j++;
            }
            out.push_back({src.substr(i, j - i), 3});
            i = j;
            continue;
        }

        // 预处理行 (# 开头, 行首位置, 处理续行)
        if (c == '#') {
            bool at_line_start = false;
            if (out.empty()) at_line_start = true;
            else {
                const Token& back = out.back();
                if (back.type == 1 && back.text.find('\n') != std::string::npos) at_line_start = true;
                else if (back.type == 4) at_line_start = true;
                else if (out.size() == 1 && back.type == 1) at_line_start = true;
            }
            if (at_line_start) {
                size_t j = i;
                while (j < n) {
                    if (src[j] == '\\' && j + 1 < n && src[j + 1] == '\n') { j += 2; continue; }
                    if (src[j] == '\n') break;
                    j++;
                }
                out.push_back({src.substr(i, j - i), 4});
                i = j;
                continue;
            }
        }

        // 标识符
        if (is_idstart(c)) {
            size_t j = i;
            while (j < n && is_idchar(src[j])) j++;
            std::string id = src.substr(i, j - i);
            // 原始字符串 R"delim(...)delim"
            if (j < n && src[j] == '"' && is_rawstr_prefix(id)) {
                size_t k = j + 1;
                size_t delim_start = k;
                while (k < n && src[k] != '(') k++;
                std::string delim = src.substr(delim_start, k - delim_start);
                if (k < n) k++; // skip '('
                std::string needle = ")" + delim + "\"";
                size_t end = src.find(needle, k);
                if (end == std::string::npos) end = n;
                else end += needle.size();
                out.push_back({src.substr(i, end - i), 2});
                i = end;
                continue;
            }
            out.push_back({id, 0});
            i = j;
            continue;
        }

        // 字符串/字符字面量
        if (c == '"' || c == '\'') {
            char quote = c;
            size_t j = i + 1;
            while (j < n && src[j] != quote) {
                if (src[j] == '\\' && j + 1 < n) j += 2;
                else if (src[j] == '\n') break;
                else j++;
            }
            if (j < n && src[j] == quote) j++;
            out.push_back({src.substr(i, j - i), 2});
            i = j;
            continue;
        }

        // 数字字面量
        if (std::isdigit((unsigned char)c)) {
            size_t j = i;
            if (c == '0' && j + 1 < n && (src[j+1]=='x'||src[j+1]=='X'||src[j+1]=='b'||src[j+1]=='B')) {
                j += 2;
                while (j < n && (std::isxdigit((unsigned char)src[j]) || src[j]=='\'')) j++;
            } else {
                while (j < n && (std::isdigit((unsigned char)src[j]) || src[j]=='\'')) j++;
                if (j < n && src[j]=='.') {
                    j++;
                    while (j < n && (std::isdigit((unsigned char)src[j]) || src[j]=='\'')) j++;
                }
                if (j < n && (src[j]=='e'||src[j]=='E')) {
                    j++;
                    if (j < n && (src[j]=='+'||src[j]=='-')) j++;
                    while (j < n && std::isdigit((unsigned char)src[j])) j++;
                }
            }
            while (j < n && (std::isalpha((unsigned char)src[j]) || src[j]=='_')) j++;
            out.push_back({src.substr(i, j - i), 0});
            i = j;
            continue;
        }

        // 多字符操作符
        static const char* ops[] = {
            "...", "->*", "<<=", ">>=",
            "::", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=",
            "&&", "||", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
            nullptr
        };
        bool matched = false;
        for (int k = 0; ops[k]; k++) {
            size_t len = std::strlen(ops[k]);
            if (i + len <= n && std::memcmp(src.data() + i, ops[k], len) == 0) {
                out.push_back({src.substr(i, len), 0});
                i += len;
                matched = true;
                break;
            }
        }
        if (matched) continue;

        // 单字符
        out.push_back({std::string(1, c), 0});
        i++;
    }
    return out;
}

// ---------- 辅助 ----------
static int prev_sig(const std::vector<Token>& t, int idx) {
    for (int k = idx - 1; k >= 0; k--) if (t[k].type != 1) return k;
    return -1;
}
static int next_sig(const std::vector<Token>& t, int idx) {
    for (int k = idx + 1; k < (int)t.size(); k++) if (t[k].type != 1) return k;
    return -1;
}

static const std::set<std::string>& type_keywords() {
    static const std::set<std::string> s = {
        "int","char","bool","short","long","float","double","void",
        "unsigned","signed","auto","const","volatile","static",
        "wchar_t","char16_t","char32_t","size_t","ptrdiff_t",
        "register","mutable","extern","inline","virtual","explicit",
        "constexpr","consteval","constinit","friend","typedef",
        "struct","class","union","enum","typename","using",
        "namespace","template","operator","public","protected","private",
        "override","final","noexcept","throw",
        "new","delete","sizeof","alignof","alignas","decltype","return",
        "true","false","nullptr","this",
        "and","or","not","xor","bitand","bitor","compl"
    };
    return s;
}

// 判断 * / & 是否处于"声明语境" (前面是类型)
// 注意: 不把 :: 视为触发条件 -- Foo::*mp 中 ::* 应保持紧贴 (成员指针)
static bool looks_like_type_context(const std::vector<Token>& t, int idx) {
    if (idx < 0) return false;
    const Token& tk = t[idx];
    if (tk.type != 0) return false;
    if (type_keywords().count(tk.text)) return true;
    if (tk.text == ">" || tk.text == ">>") return true;
    if (tk.text == "*" || tk.text == "&" || tk.text == "&&") return true;
    if (!tk.text.empty() && std::isupper((unsigned char)tk.text[0]) &&
        (tk.text.size() == 1 || is_idchar(tk.text[1]))) return true;
    return false;
}

// 从 i 处的 * / & / && 开始, 向后扫描连续的指针/引用符号链,
// 判断链尾是否紧跟一个标识符 (变量名). 用于区分:
//   int *p        -> 链尾是 p, 是声明, 应处理
//   static_cast<int*>  -> 链尾是 >, 不是声明, 不应处理
//   -> int& {     -> 链尾是 {, 不是声明, 不应处理
static bool chain_ends_with_var(const std::vector<Token>& t, int i) {
    int n = (int)t.size();
    int k = i;
    while (k < n && t[k].type == 0 &&
           (t[k].text == "*" || t[k].text == "&" || t[k].text == "&&")) {
        k++;
        while (k < n && t[k].type == 1) k++;
    }
    return (k < n && t[k].type == 0 && !t[k].text.empty() && is_idstart(t[k].text[0]));
}

// ---------- 格式化 ----------
struct Formatter {
    std::vector<Token> t;
    explicit Formatter(std::vector<Token> tok) : t(std::move(tok)) {}

    // 规则 5 + 6 + 3: if/for/while 与 ( 间一空格, 括号内首尾去空格, for 内分号后去空格
    void apply_control_paren() {
        for (int i = 0; i < (int)t.size(); i++) {
            if (t[i].type != 0) continue;
            const std::string& kw = t[i].text;
            if (!(kw == "if" || kw == "for" || kw == "while" || kw == "switch" || kw == "catch"))
                continue;
            int j = i + 1;
            while (j < (int)t.size() && t[j].type == 1) j++;
            if (j >= (int)t.size() || t[j].type != 0 || t[j].text != "(") continue;
            // 规则 6: 关键字与 ( 之间恰好一个空格
            if (j - 1 > i + 1) {
                t.erase(t.begin() + i + 1, t.begin() + j);
                j = i + 1;
            }
            if (t[i+1].type == 1) {
                t[i+1].text = " ";
            } else {
                t.insert(t.begin() + i + 1, Token{" ", 1});
            }
            int lp = i + 2; // '(' 位置
            // 规则 5: ( 之后首部去空白
            if (lp + 1 < (int)t.size() && t[lp+1].type == 1) {
                t.erase(t.begin() + lp + 1);
            }
            // 找匹配 )
            int depth = 1;
            int rp = -1;
            for (int k = lp + 1; k < (int)t.size(); k++) {
                if (t[k].type != 0) continue;
                if (t[k].text == "(") depth++;
                else if (t[k].text == ")") {
                    depth--;
                    if (depth == 0) { rp = k; break; }
                }
            }
            if (rp < 0) continue;
            // ) 之前尾部去空白
            if (rp - 1 >= 0 && t[rp-1].type == 1) {
                t.erase(t.begin() + (rp - 1));
                rp--;
            }
            // 规则 3: for 内分号两侧去空格 (仅最外层 depth==1)
            if (kw == "for") {
                int depth2 = 0;
                for (int k = lp; k <= rp; k++) {
                    if (t[k].type != 0) continue;
                    if (t[k].text == "(") depth2++;
                    else if (t[k].text == ")") depth2--;
                    else if (t[k].text == ";" && depth2 == 1) {
                        if (k - 1 >= 0 && t[k-1].type == 1) {
                            t.erase(t.begin() + (k - 1));
                            k--;
                            rp--;
                        }
                        if (k + 1 < (int)t.size() && t[k+1].type == 1) {
                            t.erase(t.begin() + (k + 1));
                            rp--;
                        }
                    }
                }
            }
        }
    }

    // 规则 2: 逗号后去空格 (保留换行)
    void apply_comma() {
        for (int i = 0; i < (int)t.size(); i++) {
            if (t[i].type != 0 || t[i].text != ",") continue;
            int j = i + 1;
            while (j < (int)t.size() && t[j].type == 1) {
                const std::string& ws = t[j].text;
                bool only_spaces_tabs = true;
                for (char c : ws) if (c != ' ' && c != '\t') { only_spaces_tabs = false; break; }
                if (only_spaces_tabs) {
                    t.erase(t.begin() + j);
                } else {
                    std::string kept;
                    for (char c : ws) if (c != ' ' && c != '\t') kept.push_back(c);
                    if (kept.empty()) t.erase(t.begin() + j);
                    else { t[j].text = kept; j++; }
                    break;
                }
            }
        }
    }

    // 规则 1: 指针/引用靠近变量名
    // 仅当 * / & 链末端紧跟变量名 (标识符) 时才视为声明并处理,
    // 否则视为类型表达式 (如 static_cast<int*>, -> int& {) 不动.
    void apply_pointer_ref() {
        for (int i = 0; i < (int)t.size(); i++) {
            if (t[i].type != 0) continue;
            const std::string& op = t[i].text;
            if (op != "*" && op != "&" && op != "&&") continue;
            int p = prev_sig(t, i);
            if (p < 0) continue;
            Token prev_tok = t[p];
            bool is_chain = prev_tok.type == 0 &&
                (prev_tok.text == "*" || prev_tok.text == "&" || prev_tok.text == "&&");
            if (!is_chain && !looks_like_type_context(t, p)) continue;
            if (!chain_ends_with_var(t, i)) continue;
            if (is_chain) {
                if (i - 1 >= 0 && t[i-1].type == 1) {
                    t.erase(t.begin() + (i - 1));
                    i--;
                }
            } else {
                if (p == i - 1) {
                    t.insert(t.begin() + i, Token{" ", 1});
                    i++;
                } else if (t[i-1].type == 1) {
                    t[i-1].text = " ";
                }
            }
            int after = i + 1;
            if (after < (int)t.size() && t[after].type == 1) {
                int nn = next_sig(t, i);
                if (nn >= 0 && t[nn].type == 0 && !t[nn].text.empty()) {
                    const std::string& nxt = t[nn].text;
                    if (is_idstart(nxt[0]) || nxt == "*" || nxt == "&" || nxt == "&&") {
                        t.erase(t.begin() + after);
                    }
                }
            }
        }
    }

    // 规则 4: 短函数体大括号内部首尾去空格
    // 条件: { 与匹配 } 在同一行, 内部不含嵌套 {}, 不超过 1 个分号
    void apply_short_braces() {
        for (int i = 0; i < (int)t.size(); i++) {
            if (t[i].type != 0 || t[i].text != "{") continue;
            int depth = 1;
            int j = -1;
            int semis = 0;
            bool nested_block = false;
            bool cross_line = false;
            for (int k = i + 1; k < (int)t.size(); k++) {
                const Token& tk = t[k];
                if (tk.type == 1) {
                    if (tk.text.find('\n') != std::string::npos) {
                        cross_line = true;
                        break;
                    }
                    continue;
                }
                if (tk.text == "{") { nested_block = true; break; }
                if (tk.text == "(" || tk.text == "[") {
                    char open_ch = tk.text[0];
                    char close_ch = (open_ch == '(' ? ')' : ']');
                    int d2 = 1;
                    k++;
                    while (k < (int)t.size() && d2 > 0) {
                        if (t[k].type == 0) {
                            if (t[k].text == std::string(1, open_ch)) d2++;
                            else if (t[k].text == std::string(1, close_ch)) d2--;
                        }
                        k++;
                    }
                    k--;
                    continue;
                }
                if (tk.text == "}") { depth--; if (depth == 0) { j = k; break; } }
                if (tk.text == ";") {
                    semis++;
                    if (semis > 1) break;
                }
            }
            if (cross_line || nested_block || j < 0 || semis > 1) continue;
            if (i + 1 < (int)t.size() && t[i+1].type == 1) {
                t.erase(t.begin() + (i + 1));
                j--;
            }
            if (j - 1 > i && t[j-1].type == 1) {
                t.erase(t.begin() + (j - 1));
            }
        }
    }

    void run() {
        apply_control_paren();
        apply_comma();
        apply_pointer_ref();
        apply_short_braces();
    }

    std::string emit() const {
        std::string s;
        s.reserve(t.size() * 4);
        for (const auto& tk : t) s += tk.text;
        return s;
    }
};

// ---------- 文件 IO ----------
static bool read_file(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}
static bool write_file(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(content.data(), (std::streamsize)content.size());
    return (bool)out;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " filename\n";
        return 1;
    }
    std::string filename = argv[1];
    std::string src;
    if (!read_file(filename, src)) {
        std::cerr << "codeformat: cannot read " << filename << "\n";
        return 1;
    }
    if (!write_file(filename + ".bak", src)) {
        std::cerr << "codeformat: cannot write backup " << filename << ".bak\n";
        return 1;
    }
    std::vector<Token> toks = tokenize(src);
    Formatter fmt(std::move(toks));
    fmt.run();
    std::string result = fmt.emit();
    if (!write_file(filename, result)) {
        std::cerr << "codeformat: cannot write " << filename << "\n";
        return 1;
    }
    std::cout << "Formatted: " << filename << " (backup: " << filename << ".bak)\n";
    return 0;
}
