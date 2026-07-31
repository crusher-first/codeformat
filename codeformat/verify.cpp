// verify.cpp - 验证 codeformat 的正确性
// 用法:
//   verify original.cpp formatted.cpp
// 检查:
//   1. token 序列一致性 (剔除空白 token 后必须完全相同, 保证语义不变)
//   2. 幂等性 (对已格式化结果再格式化一次, 必须完全相同)
// 退出码: 0 = 通过, 1 = 失败
//
// 说明: 复用 codeformat.cpp 的 tokenizer 与 Formatter.
//   通过 #include "codeformat.cpp" 引入, 用宏屏蔽其 main.
//   注意: codeformat.cpp 中的 read_file / write_file 也被一并引入,
//   这里直接复用, 不再重复定义.

// 屏蔽 codeformat.cpp 的 main, 仅复用其类型/函数
#define main codeformat_main
#include "codeformat.cpp"
#undef main

#include <iostream>
#include <string>
#include <vector>

// 提取非空白 token 的 text 列表
static std::vector<std::string> strip_ws(const std::vector<Token>& toks) {
    std::vector<std::string> r;
    for (const auto& tk : toks) if (tk.type != 1) r.push_back(tk.text);
    return r;
}

static bool check_token_equality(const std::string& orig, const std::string& fmt, const std::string& label) {
    auto a = strip_ws(tokenize(orig));
    auto b = strip_ws(tokenize(fmt));
    if (a == b) {
        std::cout << "[OK] " << label << " token sequence identical (" << a.size() << " tokens)\n";
        return true;
    }
    std::cout << "[FAIL] " << label << " token sequence differs!\n";
    size_t n = std::min(a.size(), b.size());
    for (size_t k = 0; k < n; k++) {
        if (a[k] != b[k]) {
            size_t ctx = (k >= 3 ? k - 3 : 0);
            std::cout << "  first diff at index " << k << ":\n";
            std::cout << "    orig:      ...";
            for (size_t m = ctx; m <= k && m < a.size(); m++) std::cout << "[" << a[m] << "]";
            std::cout << "\n    formatted: ...";
            for (size_t m = ctx; m <= k && m < b.size(); m++) std::cout << "[" << b[m] << "]";
            std::cout << "\n";
            break;
        }
    }
    if (a.size() != b.size())
        std::cout << "  length differs: orig=" << a.size() << " formatted=" << b.size() << "\n";
    return false;
}

static bool check_idempotent(const std::string& fmt_text, const std::string& label) {
    std::vector<Token> toks = tokenize(fmt_text);
    Formatter f2(std::move(toks));
    f2.run();
    std::string text2 = f2.emit();
    if (text2 == fmt_text) {
        std::cout << "[OK] " << label << " idempotent (second pass == first pass)\n";
        return true;
    }
    std::cout << "[FAIL] " << label << " not idempotent!\n";
    size_t p = 0;
    while (p < fmt_text.size() && p < text2.size() && fmt_text[p] == text2[p]) p++;
    std::cout << "  first char diff at offset " << p << "\n";
    return false;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " original.cpp formatted.cpp\n";
        return 1;
    }
    std::string orig_text, fmt_text;
    // 复用 codeformat.cpp 里的 read_file
    if (!read_file(argv[1], orig_text)) {
        std::cerr << "verify: cannot read " << argv[1] << "\n";
        return 1;
    }
    if (!read_file(argv[2], fmt_text)) {
        std::cerr << "verify: cannot read " << argv[2] << "\n";
        return 1;
    }
    bool ok = true;
    ok &= check_token_equality(orig_text, fmt_text, argv[1]);
    ok &= check_idempotent(fmt_text, argv[1]);
    std::cout << std::string(50, '=') << "\n";
    std::cout << "RESULT: " << (ok ? "ALL CHECKS PASSED" : "FAILED") << "\n";
    return ok ? 0 : 1;
}
