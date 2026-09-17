import re

with open(r'D:\FrustLang\projects\plot_viewer_core\src\lib.fr', 'r', encoding='utf-8') as f:
    content = f.read()

# Replace signature
content = re.sub(
    r'(pub fn generate_[a-zA-Z0-9_]+)\(buf:\s*raw\*\s*f32,\s*w:\s*i64,\s*h:\s*i64,\s*start_y:\s*i64,\s*end_y:\s*i64\)',
    r'\1(buf: raw* f32, w: i64, h: i64, packed_y: i64)',
    content
)

# Insert the unpacking logic
content = re.sub(
    r'let mut py: i64 = start_y;',
    r'let start_y: i64 = packed_y % 10000;\n    let end_y: i64 = packed_y / 10000;\n    let mut py: i64 = start_y;',
    content
)

with open(r'D:\FrustLang\projects\plot_viewer_core\src\lib.fr', 'w', encoding='utf-8') as f:
    f.write(content)
