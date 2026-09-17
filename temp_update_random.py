with open(r'D:\FrustLang\projects\06_frust_library\core\src\random.fr', 'r', encoding='utf-8') as f:
    content = f.read()

new_code = '''
// Logical right shift for 64-bit integers.
// (Workaround for arithmetic shift behavior)
pub fn lshr(x: i64, k: i64) -> i64 = {
    let mut mask: i64 = 1;
    mask = (mask << (64 - k)) - 1;
    return (x >> k) & mask;
}

// Left rotate for 64-bit integers.
pub fn rotl(x: i64, k: i64) -> i64 = {
    let right_shift: i64 = 64 - k;
    let mut mask: i64 = 1;
    mask = (mask << k) - 1;
    return (x << k) | ((x >> right_shift) & mask);
}

// SplitMix64 generator, ideal for initializing other PRNG states
// from a single 64-bit seed.
struct SplitMix64 {
    state: i64
}

impl SplitMix64 {
    pub fn next(&mut self) -> i64 = {
        self.state = self.state + -7046029254386353131; // 0x9e3779b97f4a7c15
        let mut z: i64 = self.state;
        z = (z ^ lshr(z, 30)) * -4658895280553007687; // 0xbf58476d1ce4e5b9
        z = (z ^ lshr(z, 27)) * -7723592293110705685; // 0x94d049bb133111eb
        return z ^ lshr(z, 31);
    }
}

pub fn splitmix64_init(seed: i64) -> SplitMix64 = {
    SplitMix64 { state: seed }
}

// Xoshiro256** - A fast, modern PRNG ideal for games and simulations.
struct Xoshiro256 {
    s0: i64,
    s1: i64,
    s2: i64,
    s3: i64
}

impl Xoshiro256 {
    pub fn next(&mut self) -> i64 = {
        let result: i64 = rotl(self.s1 * 5, 7) * 9;
        let t: i64 = self.s1 << 17;

        self.s2 = self.s2 ^ self.s0;
        self.s3 = self.s3 ^ self.s1;
        self.s1 = self.s1 ^ self.s2;
        self.s0 = self.s0 ^ self.s3;

        self.s2 = self.s2 ^ t;
        self.s3 = rotl(self.s3, 45);

        return result;
    }

    pub fn next_f64(&mut self) -> f64 = {
        // Generate a 53-bit float in [0.0, 1.0)
        let v: i64 = lshr(self.next(), 11);
        v / 9007199254740992.0 // divide by 2^53
    }
    
    pub fn next_range(&mut self, lo: i64, hi: i64) -> i64 = {
        let span: i64 = hi - lo;
        lo + (lshr(self.next(), 1) % span)
    }
}

pub fn xoshiro256_init(seed: i64) -> Xoshiro256 = {
    let mut sm: SplitMix64 = SplitMix64 { state: seed };
    Xoshiro256 {
        s0: sm.next(),
        s1: sm.next(),
        s2: sm.next(),
        s3: sm.next()
    }
}

'''

content = content + "\\n" + new_code

with open(r'D:\FrustLang\projects\06_frust_library\core\src\random.fr', 'w', encoding='utf-8') as f:
    f.write(content)
