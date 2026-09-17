with open(r'D:\FrustLang\projects\01_language_paradigms\02_functional\Codegen.h', 'r') as f:
    lines = f.readlines()

for i, line in enumerate(lines):
    if 'llvm::Function* compileFunction(const FunctionDecl& fn)' in line:
        for j in range(i+35, i+200):
            if 'return llvmFn;' in lines[j] or 'return nullptr;' in lines[j]:
                if 'builder.CreateRet' in lines[j-5:j+5] or 'verifyFunction' in lines[j-10:j+5]:
                    print(''.join(lines[j-10:j+10]))
                    break
