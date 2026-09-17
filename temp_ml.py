with open(r'D:\FrustLang\projects\01_language_paradigms\02_functional\ModuleLoader.cpp', 'r') as f:
    lines = f.readlines()

for i, line in enumerate(lines):
    if 'prefix' in line:
        print(''.join(lines[i-2:i+30]))
        break
