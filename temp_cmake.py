with open(r'D:\FrustLang\projects\10_plot_viewer\CMakeLists.txt', 'r') as f:
    code = f.read()

if 'frust_runtime' not in code:
    code = code.replace(
        'juce::juce_gui_basics',
        'juce::juce_gui_basics\n    "\/../../lib/$<CONFIG>/frust_runtime.lib"'
    )

with open(r'D:\FrustLang\projects\10_plot_viewer\CMakeLists.txt', 'w') as f:
    f.write(code)
