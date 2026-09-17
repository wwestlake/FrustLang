with open(r'D:\FrustLang\projects\05_frate\src\main.cpp', 'r') as f:
    lines = f.readlines()

for i, line in enumerate(lines):
    if 'static bool collectImportedPodFiles(' in line:
        print(''.join(lines[i+30:i+60]))
        break
