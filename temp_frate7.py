import re
with open(r'D:\FrustLang\projects\05_frate\src\main.cpp', 'r') as f:
    content = f.read()

content = content.replace(
    'if (!compilerExe.existsAsFile()) {',
    'if (!compilerExe.existsAsFile()) {\n        std::cerr << "Tested: " << compilerExe.getFullPathName() << "\\n";'
)

with open(r'D:\FrustLang\projects\05_frate\src\main.cpp', 'w') as f:
    f.write(content)
