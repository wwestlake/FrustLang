with open(r'D:\FrustLang\projects\01_language_paradigms\02_functional\Codegen.h', 'r') as f:
    code = f.read()

code = code.replace(
    'hadCodegenError = true;\n            return nullptr;',
    'hadCodegenError = true;\n            std::cerr << "DEBUG: blockTerminated is " << blockTerminated << "\\n";\n            return nullptr;'
)

with open(r'D:\FrustLang\projects\01_language_paradigms\02_functional\Codegen.h', 'w') as f:
    f.write(code)
