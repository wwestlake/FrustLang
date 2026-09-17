with open(r'D:\FrustLang\projects\01_language_paradigms\02_functional\Codegen.h', 'r') as f:
    lines = f.readlines()

found = 0
for i, line in enumerate(lines):
    if 'case ExprKind::Block:' in line:
        found += 1
        if found == 2:
            print(''.join(lines[i:i+60]))
            break
