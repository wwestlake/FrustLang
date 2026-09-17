with open(r'D:\FrustLang\projects\01_language_paradigms\02_functional\Main.cpp', 'r') as f:
    lines = f.readlines()

for i, line in enumerate(lines):
    if 'ResolveImports' in line:
        print(''.join(lines[i-15:i+15]))
