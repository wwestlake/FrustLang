import re
with open(r'D:\FrustLang\projects\plot_viewer_core\src\lib.fr', 'r') as f:
    content = f.read()

content = re.sub(r'use core::random::xoshiro256_init;\n', '', content)
content = re.sub(r'use core::random::Xoshiro256;\n', '', content)
content = re.sub(r'use core::mem::malloc;\n', '', content)

content = re.sub(r'\bmalloc\(', 'core::malloc(', content)
content = re.sub(r'\bxoshiro256_init\(', 'core::xoshiro256_init(', content)
content = re.sub(r'\bXoshiro256\b', 'core::Xoshiro256', content)

with open(r'D:\FrustLang\projects\plot_viewer_core\src\lib.fr', 'w') as f:
    f.write(content)
