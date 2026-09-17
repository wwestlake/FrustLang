with open(r'D:\FrustLang\projects\01_language_paradigms\02_functional\Codegen.h', 'r') as f:
    lines = f.readlines()

in_func = False
func_start = 0
for i, line in enumerate(lines):
    if 'llvm::Function* compileFunction(const FunctionDecl& fn)' in line:
        in_func = True
        func_start = i
    elif in_func and line.startswith('    }'):
        print(''.join(lines[i-15:i+1]))
        break
