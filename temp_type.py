with open(r'D:\FrustLang\projects\01_language_paradigms\02_functional\grammar\frust.y', 'r') as f:
    lines = f.readlines()

for i, line in enumerate(lines):
    if 'type:' in line:
        print(''.join(lines[i-2:i+15]))
        break
