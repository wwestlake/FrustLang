#include <fstream>
void test_file() {
    std::ofstream out("D:\\FrustLang\\projects\\10_plot_viewer\\test.txt");
    out << "TEST" << std::endl;
}
int main() { test_file(); return 0; }
