import re

with open(r'D:\FrustLang\projects\plot_viewer_core\src\lib.fr', 'r', encoding='utf-8') as f:
    content = f.read()

content = re.sub(
    r'let mut py: i64 = 0;\s*while\s*\(py\s*<\s*h\)',
    r'let mut py: i64 = start_y;\n    while (py < end_y)',
    content
)

with open(r'D:\FrustLang\projects\plot_viewer_core\src\lib.fr', 'w', encoding='utf-8') as f:
    f.write(content)
