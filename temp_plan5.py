with open(r'D:\FrustLang\projects\01_language_paradigms\02_functional\ModuleLoader.cpp', 'r') as f:
    lines = f.readlines()

for i, line in enumerate(lines):
    if 'podsToImport.push_back' in line:
        print(''.join(lines[i-15:i+15]))
        break
