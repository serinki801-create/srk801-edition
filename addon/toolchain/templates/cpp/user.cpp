#include <cstdio>
#include <vector>
#include <numeric>

/*
 * C++-шаблон: ваша логика здесь (STL, шаблоны, — всё что умеет clang).
 * Текст результата в g_msg появляется на экране.
 */

extern char g_msg[512];

static long sum_squares(int n) {
    std::vector<int> v(n);
    for (int i = 0; i < n; i++) v[i] = i * i;
    return std::accumulate(v.begin(), v.end(), 0L);
}

extern "C" void user_main(void) {
    const int n = 1000;
    std::snprintf(g_msg, sizeof(g_msg),
                  "Hello from C++!\nsum(i^2, i=0..%d) = %ld\nSTL, clang, iOS arm64",
                  n, sum_squares(n));
}
