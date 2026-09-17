import re
with open(r'D:\FrustLang\projects\01_language_paradigms\02_functional\Codegen.h', 'r') as f:
    content = f.read()

content = re.sub(
    r'llvm::Function\*\s*callee\s*=\s*module\.getFunction\(fnName\);',
    r'llvm::Function* callee = module.getFunction(fnName);\n        if (!callee && !currentNamespace.empty()) {\n            callee = module.getFunction(currentNamespace + "::" + fnName);\n        }',
    content
)

with open(r'D:\FrustLang\projects\01_language_paradigms\02_functional\Codegen.h', 'w') as f:
    f.write(content)
