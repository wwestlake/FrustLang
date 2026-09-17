with open(r'D:\FrustLang\projects\10_plot_viewer\CMakeLists.txt', 'r') as f:
    code = f.read()

code = code.replace(
    '"\/../../lib/$<CONFIG>/frust_runtime.lib"',
    '"D:/FrustLang/lib/$<CONFIG>/frust_runtime.lib"'
)

with open(r'D:\FrustLang\projects\10_plot_viewer\CMakeLists.txt', 'w') as f:
    f.write(code)
