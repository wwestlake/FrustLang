with open(r'D:\FrustLang\projects\01_language_paradigms\02_functional\Codegen.h', 'r') as f:
    code = f.read()

code = code.replace(
    'return ok;\n    }',
    'return ok && !hadCodegenError;\n    }'
)

with open(r'D:\FrustLang\projects\01_language_paradigms\02_functional\Codegen.h', 'w') as f:
    f.write(code)
