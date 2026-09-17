import os
import re

main_cpp_path = r'D:\FrustLang\projects\10_plot_viewer\Source\Main.cpp'
with open(main_cpp_path, 'r') as f:
    code = f.read()

# I want to add a print statement inside host_set_noise_perm_val
new_func = '''extern "C" void host_set_noise_perm_val(int64_t idx, int64_t val) {
    if (idx == 0) {
        FILE* f = fopen("perm_dump.txt", "w");
        fprintf(f, "--- NEW SEED ---\n");
        fclose(f);
    }
    if (idx >= 0 && idx < 256) {
        FILE* f = fopen("perm_dump.txt", "a");
        fprintf(f, "%lld ", val);
        fclose(f);
        
        p512[idx] = (int)val;
        p512[idx + 256] = (int)val;
        p12[idx] = (int)val % 12;
        p12[idx + 256] = (int)val % 12;
    }
}'''

code = re.sub(r'extern "C" void host_set_noise_perm_val.*?\}', new_func, code, flags=re.DOTALL)

with open(main_cpp_path, 'w') as f:
    f.write(code)
