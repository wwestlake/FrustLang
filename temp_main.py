with open(r'D:\FrustLang\projects\10_plot_viewer\Source\Main.cpp', 'r') as f:
    code = f.read()

code = code.replace(
'''extern "C" int64_t frust_buf_get_i64(void* base, int64_t idx) {
    return ((int64_t*)base)[idx];
}

extern "C" void frust_buf_set_i64(void* base, int64_t idx, int64_t val) {
    ((int64_t*)base)[idx] = val;
}''', ''
)

with open(r'D:\FrustLang\projects\10_plot_viewer\Source\Main.cpp', 'w') as f:
    f.write(code)
