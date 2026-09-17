with open(r'D:\FrustLang\projects\plot_viewer_core\src\lib.fr', 'r', encoding='utf-8') as f:
    lines = f.readlines()

new_lines = []
uses = []

for line in lines:
    if line.strip().startswith('use '):
        if 'use core' in line:
            if line not in uses:
                uses.append(line)
    else:
        new_lines.append(line)

final_lines = uses + new_lines

with open(r'D:\FrustLang\projects\plot_viewer_core\src\lib.fr', 'w', encoding='utf-8') as f:
    f.writelines(final_lines)
