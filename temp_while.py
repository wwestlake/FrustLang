with open(r'D:\FrustLang\projects\01_language_paradigms\02_functional\Codegen.h', 'r') as f:
    lines = f.readlines()

for i, line in enumerate(lines):
    if 'llvm::Value* compileWhile(' in line:
        print(''.join(lines[i:i+40]))
        break
