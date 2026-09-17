import re

with open(r'D:\FrustLang\projects\05_frate\src\main.cpp', 'r') as f:
    content = f.read()

# Remove the 'isCrossPodImport' hack for 'use pod;'
new_content = re.sub(
    r'\} else if \(line\.startsWith\("use "\) && !line\.startsWith\("use self::"\)\) \{.*?\isCrossPodImport = true;\n\s*\}',
    r'}',
    content,
    flags=re.DOTALL
)

with open(r'D:\FrustLang\projects\05_frate\src\main.cpp', 'w') as f:
    f.write(new_content)
