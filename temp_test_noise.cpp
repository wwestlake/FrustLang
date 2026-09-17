#include <iostream>
#include <vector>

extern "C" void seed_procedural_noise(long long seed);
extern "C" long long frust_buf_get_i64(void* base, long long idx) { return ((long long*)base)[idx]; }
extern "C" void frust_buf_set_i64(void* base, long long idx, long long val) { ((long long*)base)[idx] = val; }

int p512[512];
extern "C" void host_set_noise_perm_val(long long idx, long long val) {
    if (idx >= 0 && idx < 256) p512[idx] = (int)val;
}

int main() {
    seed_procedural_noise(12345);
    std::cout << "First 10 values of perm array:" << std::endl;
    for(int i=0; i<10; i++) std::cout << p512[i] << " ";
    std::cout << std::endl;
    return 0;
}
