import re
with open(r'D:\FrustLang\projects\05_frate\src\main.cpp', 'r') as f:
    content = f.read()

content = re.sub(
    r'args\.add\("--emit-obj"\);\n\s*args\.add\(objOutPath\);',
    r'args.add("--emit-obj");\n        args.add(objOutPath);\n        args.add("--namespace");\n        args.add(config.getMetadata().name);',
    content
)

with open(r'D:\FrustLang\projects\05_frate\src\main.cpp', 'w') as f:
    f.write(content)
