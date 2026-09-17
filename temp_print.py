with open(r'D:\FrustLang\projects\10_plot_viewer\Source\Main.cpp', 'r') as f:
    code = f.read()

code = code.replace(
    'seed_procedural_noise((int64_t)time(NULL));',
    '''seed_procedural_noise((int64_t)time(NULL));
        std::cout << "DEBUG: p512[1] = " << p512[1] << ", p512[2] = " << p512[2] << std::endl;'''
)

with open(r'D:\FrustLang\projects\10_plot_viewer\Source\Main.cpp', 'w') as f:
    f.write(code)
