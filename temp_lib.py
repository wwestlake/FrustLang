with open(r'D:\FrustLang\projects\plot_viewer_core\src\lib.fr', 'r') as f:
    code = f.read()

# Add use core;
if 'use core;' not in code:
    code = 'use core;\n' + code

# Restore frust_buf_set_i64 to buffer::set_i64 and remove externs
code = code.replace('extern fn frust_buf_frust_buf_get_i64(base: String, idx: i64) -> i64;', '')
code = code.replace('extern fn frust_buf_frust_buf_set_i64(base: String, idx: i64, val: i64);', '')
code = code.replace('frust_buf_get_i64(', 'core::buffer::get_i64(')
code = code.replace('frust_buf_set_i64(', 'core::buffer::set_i64(')

with open(r'D:\FrustLang\projects\plot_viewer_core\src\lib.fr', 'w') as f:
    f.write(code)
