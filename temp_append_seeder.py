with open(r'D:\FrustLang\projects\plot_viewer_core\src\lib.fr', 'r', encoding='utf-8') as f:
    content = f.read()

new_code = '''
extern fn host_set_noise_perm_val(idx: i64, val: i64);

use core::random::xoshiro256_init;
use core::random::Xoshiro256;

// Seeds the C++ Simplex noise permutation table using our Frust RNG.
pub fn seed_procedural_noise(seed: i64) = {
    let mut rng: Xoshiro256 = xoshiro256_init(seed);
    
    // Frust doesn't have local array variables yet, so we use a raw memory buffer
    use core::mem::malloc;
    use core::mem::free;
    use core::buffer::buf_set;
    use core::buffer::buf_get;
    
    // Allocate 256 i64s
    let mut perm: raw* i64 = malloc(256 * 8) as raw* i64;
    
    let mut i: i64 = 0;
    while (i < 256) {
        buf_set(perm, i, i);
        i = i + 1;
    };
    
    // Fisher-Yates shuffle
    i = 255;
    while (i > 0) {
        let j: i64 = rng.next_range(0, i + 1);
        let tmp: i64 = buf_get(perm, i);
        buf_set(perm, i, buf_get(perm, j));
        buf_set(perm, j, tmp);
        i = i - 1;
    };
    
    // Send to C++
    i = 0;
    while (i < 256) {
        host_set_noise_perm_val(i, buf_get(perm, i));
        i = i + 1;
    };
    
    free(perm as raw* i8);
}
'''

content = content + "\n" + new_code

with open(r'D:\FrustLang\projects\plot_viewer_core\src\lib.fr', 'w', encoding='utf-8') as f:
    f.write(content)
