import os
with open(r'D:\FrustLang\projects\monolithic_graph_test\src\lib.fr', 'r', encoding='utf-8') as f1:
    content1 = f1.read()
with open(r'D:\FrustLang\projects\frust_image_demo\src\lib.fr', 'r', encoding='utf-8') as f2:
    content2 = f2.read()

# Remove duplicate externs from content2
import re
extern_pattern = re.compile(r'extern\s+fn\s+[^;]+;')
externs1 = set(extern_pattern.findall(content1))
externs2 = set(extern_pattern.findall(content2))
for e in externs2:
    if e in externs1:
        content2 = content2.replace(e, f"// dup: {e}")

# Remove duplicate fns like lerp or smoothstep
if 'pub fn lerp' in content1 and 'pub fn lerp' in content2:
    # Just comment out the second one
    content2 = re.sub(r'pub\s+fn\s+lerp.*?}\s*', '// dup lerp removed\n', content2, flags=re.DOTALL)

with open(r'D:\FrustLang\projects\plot_viewer_core\src\lib.fr', 'w', encoding='utf-8') as fout:
    fout.write(content1 + '\n\n' + content2)
