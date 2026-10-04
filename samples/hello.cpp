// samples/hello.cpp — хост-сэмпл C++ (g++ -O2) + встроенная команда `cxx` в AI-OS.
#include <iostream>
#include <vector>
int main() {
    std::vector<int> v{1, 2, 3, 4, 5};
    int acc = 0;
    for (int x : v) acc += x * x;
    std::cout << "hello from C++ (sum sq = " << acc << ")" << std::endl;
    return 0;
}
