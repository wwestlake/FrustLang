with open(r'D:\FrustLang\projects\plot_viewer_core\src\lib.fr', 'r', encoding='utf-8') as f:
    code = f.read()

# Replace set_i64 with frust_buf_set_i64
# Remove 'use core::buffer...' 
import re

new_code = code.replace('use core::buffer::set_i64;\n', '')
new_code = new_code.replace('use core::buffer::get_i64;\n', '')

new_code = new_code.replace('extern fn host_set_noise_perm_val(idx: i64, val: i64);',
'''extern fn host_set_noise_perm_val(idx: i64, val: i64);
extern fn frust_buf_get_i64(base: String, idx: i64) -> i64;
extern fn frust_buf_set_i64(base: String, idx: i64, val: i64);''')

new_code = new_code.replace('set_i64(', 'frust_buf_set_i64(')
new_code = new_code.replace('get_i64(', 'frust_buf_get_i64(')

with open(r'D:\FrustLang\projects\plot_viewer_core\src\lib.fr', 'w', encoding='utf-8') as f:
    f.write(new_code)
