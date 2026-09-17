import re

with open(r'D:\FrustLang\projects\plot_viewer_core\src\lib.fr', 'r', encoding='utf-8') as f:
    content = f.read()

# Replace the signature: pub fn generate_hills(buf: raw* f32, w: i64, h: i64) = {
content = re.sub(
    r'(pub fn generate_[a-zA-Z0-9_]+)\(buf:\s*raw\*\s*f32,\s*w:\s*i64,\s*h:\s*i64\)',
    r'\1(buf: raw* f32, w: i64, h: i64, start_y: i64, end_y: i64)',
    content
)

# Replace the loop start:
# let mut py: i64 = 0;
# while (py < h) {
content = re.sub(
    r'let mut py:\s*i64\s*=\s*0;\s*while\s*\(py\s*<\s*h\)',
    r'let mut py: i64 = start_y;\n    while (py < end_y)',
    content
)

with open(r'D:\FrustLang\projects\plot_viewer_core\src\lib.fr', 'w', encoding='utf-8') as f:
    f.write(content)

print("Done rewrite")
