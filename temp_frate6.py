import re
with open(r'D:\FrustLang\projects\05_frate\src\main.cpp', 'r') as f:
    content = f.read()

content = content.replace(
    'juce::File compilerExe = resolveSiblingTool("frust_compiler.exe");',
    'juce::File compilerExe = resolveSiblingTool("frust_compiler_x.exe");'
)

with open(r'D:\FrustLang\projects\05_frate\src\main.cpp', 'w') as f:
    f.write(content)
