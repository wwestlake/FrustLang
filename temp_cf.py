with open(r'D:\FrustLang\projects\01_language_paradigms\02_functional\Codegen.h', 'r') as f:
    lines = f.readlines()

for i, line in enumerate(lines):
    if 'llvm::Function* compileFunction(' in line:
        print(''.join(lines[i+110:i+160]))
        break
