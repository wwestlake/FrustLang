import re
with open(r'D:\FrustLang\projects\plot_viewer_core\src\lib.fr', 'r') as f:
    content = f.read()

content = re.sub(r'\bset_i64\(', 'core::set_i64(', content)
content = re.sub(r'\bget_i64\(', 'core::get_i64(', content)

with open(r'D:\FrustLang\projects\plot_viewer_core\src\lib.fr', 'w') as f:
    f.write(content)
