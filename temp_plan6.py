with open(r'D:\FrustLang\projects\01_language_paradigms\02_functional\Codegen.h', 'r') as f:
    lines = f.readlines()

for i, line in enumerate(lines):
    if 'case ExprKind::Path:' in line:
        print(''.join(lines[i-2:i+15]))
        break
