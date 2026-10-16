//! CPython's integer-seeded MT19937 operations used by the compatibility search.
//! The native tool never starts Python to generate random numbers.
use anyhow::{Context, Result, ensure};

pub struct Random {
    state: [u32; 624],
    index: usize,
}
impl Random {
    pub fn new(seed: &str) -> Result<Self> {
        let unsigned = seed.strip_prefix(['-', '+']).unwrap_or(seed);
        ensure!(
            !unsigned.is_empty() && unsigned.chars().all(|c| c.is_ascii_digit()),
            "seed must be an integer"
        );
        let mut key = vec![0u32];
        for c in unsigned.bytes() {
            let mut carry = u64::from(c - b'0');
            for word in &mut key {
                let value = u64::from(*word) * 10 + carry;
                *word = value as u32;
                carry = value >> 32;
            }
            if carry != 0 {
                key.push(carry as u32);
            }
        }
        while key.len() > 1 && key.last() == Some(&0) {
            key.pop();
        }
        let mut result = Self {
            state: [0; 624],
            index: 624,
        };
        result.state[0] = 19650218;
        for i in 1..624 {
            result.state[i] = 1812433253u32
                .wrapping_mul(result.state[i - 1] ^ (result.state[i - 1] >> 30))
                .wrapping_add(i as u32);
        }
        let (mut i, mut j) = (1, 0);
        for _ in 0..624.max(key.len()) {
            result.state[i] = (result.state[i]
                ^ ((result.state[i - 1] ^ (result.state[i - 1] >> 30)).wrapping_mul(1664525)))
            .wrapping_add(key[j])
            .wrapping_add(j as u32);
            i += 1;
            j += 1;
            if i >= 624 {
                result.state[0] = result.state[623];
                i = 1;
            }
            if j >= key.len() {
                j = 0;
            }
        }
        for _ in 0..623 {
            result.state[i] = (result.state[i]
                ^ ((result.state[i - 1] ^ (result.state[i - 1] >> 30)).wrapping_mul(1566083941)))
            .wrapping_sub(i as u32);
            i += 1;
            if i >= 624 {
                result.state[0] = result.state[623];
                i = 1;
            }
        }
        result.state[0] = 0x80000000;
        Ok(result)
    }
    pub fn word(&mut self) -> u32 {
        if self.index >= 624 {
            for i in 0..624 {
                let y = (self.state[i] & 0x80000000) | (self.state[(i + 1) % 624] & 0x7fffffff);
                self.state[i] = self.state[(i + 397) % 624]
                    ^ (y >> 1)
                    ^ if y & 1 != 0 { 0x9908b0df } else { 0 };
            }
            self.index = 0;
        }
        let mut y = self.state[self.index];
        self.index += 1;
        y ^= y >> 11;
        y ^= (y << 7) & 0x9d2c5680;
        y ^= (y << 15) & 0xefc60000;
        y ^= y >> 18;
        y
    }
    pub fn random(&mut self) -> f64 {
        let a = self.word() >> 5;
        let b = self.word() >> 6;
        (f64::from(a) * 67108864.0 + f64::from(b)) / 9007199254740992.0
    }
    pub fn bits(&mut self, width: u32) -> Result<u64> {
        ensure!(width <= 64, "at most 64 random bits supported");
        if width == 0 {
            return Ok(0);
        }
        if width <= 32 {
            return Ok(u64::from(self.word() >> (32 - width)));
        }
        let low = u64::from(self.word());
        let high = u64::from(self.word() >> (64 - width));
        Ok(low | (high << 32))
    }
    pub fn below(&mut self, n: usize) -> Result<usize> {
        ensure!(n > 0, "empty random choice");
        let width = usize::BITS - n.leading_zeros();
        loop {
            let v = self.bits(width)?;
            if v < (n as u64) {
                return Ok(v as usize);
            }
        }
    }
    pub fn sample_two(&mut self, n: usize) -> Result<(usize, usize)> {
        ensure!(n >= 2, "sample needs at least two nodes");
        let first = self.below(n)?;
        if n <= 21 {
            let second = self.below(n - 1)?;
            Ok((first, if second == first { n - 1 } else { second }))
        } else {
            loop {
                let second = self.below(n)?;
                if second != first {
                    return Ok((first, second));
                }
            }
        }
    }
    pub fn choice<'a, T>(&mut self, values: &'a [T]) -> Result<&'a T> {
        values
            .get(self.below(values.len())?)
            .context("random choice outside collection")
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn matches_cpython_known_integer_seed() {
        let mut rng = Random::new("1").unwrap();
        assert_eq!(rng.random(), 0.13436424411240122);
        assert_eq!(rng.random(), 0.8474337369372327);
        let mut a = Random::new("123456789012345678901234567890").unwrap();
        let mut b = Random::new("-123456789012345678901234567890").unwrap();
        for _ in 0..700 {
            assert_eq!(a.word(), b.word());
        }
    }
}
