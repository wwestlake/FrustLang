with open(r'D:\FrustLang\projects\01_language_paradigms\02_functional\Codegen.h', 'r') as f:
    lines = f.readlines()

for i, line in enumerate(lines):
    if 'llvm::Function* compileFunction(const FunctionDecl& fn)' in line:
        print(''.join(lines[i+35:i+60]))
        break
