// sample.cpp - 测试用例: 覆盖各格式规范及边界情况
#include <iostream>
#include <vector>
#include <string>

#define MAX(a, b) ((a) > (b) ? (a) : (b))

template<typename T,typename U>
class Foo {
public:
    // 指针/引用: 应靠近变量名
    int *pA;
    double &refB;
    int *pBad;
    int &rBad;
    int **ppA;
    char *cp,&cr;
    Foo<int,double> *fp;
    std::vector<int> &vec;

    // 函数形参
    void func(int a,int b,double c) {
        int x = 5,y = 10;
        std::cout << "hello, world" << std::endl;
        std::cout << "a, b" << ", c" << std::endl;
    }

    // 短函数体
    bool isEmpty() {return true;}
    int  size()  {return 0;}
    void noop() {;}

    // 引用参数
    void bind(int &x,int &y) {
        x = 1; y = 2;
    }

    // for / if / while
    void loop() {
        for (int i = 0;i < 10;++i) {
            std::cout << i << ", ";
        }
        for (int i = 0;i < 10;++i) {
            // comment with , and ;
            if (i > 5) {break;}
        }
        if (true) {return;}
        if (true){doSomething();}
        while (x > 0) {--x;}
    }

    // 模板与逗号
    std::pair<int,int> pair;
    std::map<std::string,std::vector<int>> map;

    // 转义字符串里包含分隔符
    const char *str = "a, b; c * d & e";
    const char *str2 = "func(a, b)";
    char lit = 'x';

    // 指针解引用与乘法 (不应被规则 1 误伤)
    int mul(int a,int b) {
        int c = a * b;
        int *p = &c;
        *p = *p * 2;
        return *p;
    }

    // 原始字符串
    const char *raw = R"(line1, line2; line3)";
    const char *raw2 = R"DELIM(
        int* A, int& B
    )DELIM";

    // 块注释中含分隔符, 不应被处理
    /* a, b; c * d & e */
    // 行注释 a, b; c

    // 位运算的 & 不应被改 (这里 a & b 是位与)
    int bits(int a,int b) {
        return a & b;
    }
};

int main(int argc,char **argv) {
    Foo<int,double> f;
    f.func(1,2,3.0);
    return MAX(argc,1);
}
