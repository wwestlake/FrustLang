with open(r'D:\FrustLang\projects\05_frate\src\main.cpp', 'r') as f:
    lines = f.readlines()

for i, line in enumerate(lines):
    if 'isCrossPodImport = true;' in line:
        print(''.join(lines[i-10:i+5]))
