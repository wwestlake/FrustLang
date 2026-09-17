with open(r'D:\FrustLang\projects\plot_viewer_core\src\lib.fr', 'r', encoding='utf-8') as f:
    lines = f.readlines()

new_lines = []
uses = [
    'use core::random::xoshiro256_init;\n',
    'use core::random::Xoshiro256;\n',
    'use core::mem::malloc;\n',
    'use core::mem::free;\n',
    'use core::buffer::set_i64;\n',
    'use core::buffer::get_i64;\n'
]

for line in lines:
    if line.strip().startswith('use '):
        pass
    else:
        new_lines.append(line)

with open(r'D:\FrustLang\projects\plot_viewer_core\src\lib.fr', 'w', encoding='utf-8') as f:
    f.writelines(uses + new_lines)
