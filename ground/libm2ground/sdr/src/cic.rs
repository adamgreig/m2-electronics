use super::Real;

/// Qth order CIC filter with decimate-by-R (and D=R).
pub struct CIC {
    q: usize,
    r: usize,
    intg: Vec<i32>,
    comb: Vec<i32>,
    gain_shift: usize
}

impl CIC {
    /// Create a new CIC filter of order q and downsampling ratio r.
    ///
    /// Note that 12 + ceil(Q log2 R) must <= 32.
    pub fn new(q: usize, r: usize) -> CIC {
        // Verify that 12 + ceil(Q log2 R) <= 32.
        assert!(12f32 + ((q as f32) * (r as f32).log2()).ceil() <= 32f32);

        // Allocate the integrator and comb registers.
        let mut intg = Vec::with_capacity(q);
        let mut comb = Vec::with_capacity(q+1);

        for _ in 0..q {
            intg.push(0i32);
            comb.push(0i32);
        }

        // One final comb register for holding the output.
        comb.push(0i32);

        // Compute the filter gain, Q**R, as an equivalent bit shift
        let gain_shift: usize = (q as f32 * (r as f32).log2()).ceil() as usize;

        CIC { q: q, r: r, intg: intg, comb: comb, gain_shift: gain_shift }
    }

    /// Run the CIC filter over a block x,
    /// returning the filtered and decimated output.
    pub fn process(&mut self, x: &Vec<Real<i16>>) -> Vec<Real<i16>> {
        // Check we were initialised correctly
        assert!(self.q > 0 && self.r > 0);
        assert_eq!(self.intg.len(), self.q);
        assert_eq!(self.comb.len(), self.q + 1);

        // To decimate by R we need a multiple of R in the input.
        assert_eq!(x.len() % self.r, 0);

        // Output will have 1/R the number of samples of the input.
        let mut y: Vec<Real<i16>> = Vec::with_capacity(x.len() / self.r);
        unsafe { y.set_len(x.len() / self.r) };

        // Push samples through the chain.
        // It's a fair bit faster to loop over the outputs and have a tighter
        // loop over the inputs.
        for k in 0usize..y.len() {
            for o in 0..self.r {
                // Add up the integrators. Note that this cascaded approach adds
                // additional time delay (but not much!) but is easier to compute.
                unsafe { 
                    *self.intg.get_unchecked_mut(0) =
                        self.intg.get_unchecked(0)
                                 .wrapping_add(*x.get_unchecked(k*self.r+o) as i32);

                    for l in 1usize..self.q {
                        *self.intg.get_unchecked_mut(l) =
                            self.intg.get_unchecked(l)
                                     .wrapping_add(*self.intg.get_unchecked(l - 1));
                    }

                }
            }

            // Run the comb section at 1/R the sample rate
            // Each comb register is set to the output of the decimator,
            // minus all the combs before itself
            for l in 0usize..(self.q + 1) {
                let l = self.q - l;
                unsafe {
                    *self.comb.get_unchecked_mut(l) =
                        *self.intg.get_unchecked(self.q - 1);

                    for m in 0usize..l {
                        *self.comb.get_unchecked_mut(l) =
                            self.comb.get_unchecked(l)
                                     .wrapping_sub(*self.comb.get_unchecked(m));
                    }
                }
            }

            // The output is the final "output" comb register, scaled to
            // give unity filter gain.
            unsafe { *y.get_unchecked_mut(k) =
                        (*self.comb.get_unchecked(self.q) >> self.gain_shift) as i16 }
        }

        y
    }
}

#[cfg(test)]
mod tests {
    use super::CIC;

    /// Try 1st order R=2 CIC, a simple moving average filter in practice
    #[test]
    fn test_cic_1_2() {
        let mut cic = CIC::new(1, 2);
        let x = vec!{1i16, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        let y = cic.process(&x);
        assert_eq!(y, vec!{1i16, 1, 1, 1, 1, 1, 1, 1});

        let x = vec!{1i16, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3};
        let mut cic = CIC::new(1, 2);
        let y = cic.process(&x);
        assert_eq!(y, vec!{2i16, 2, 2, 2, 2, 2, 2, 2});
    }

    /// Try a bigger filter
    #[test]
    fn test_cic_2_4() {
        let x = vec!{1i16, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3};
        let mut cic = CIC::new(2, 4);
        let y = cic.process(&x);
        assert_eq!(y, vec!{1i16, 2, 2, 2});
    }

    /// Now we're cooking with gas!
    #[test]
    fn test_cic_5_8() {
        let mut x: Vec<i16> = Vec::with_capacity(8*8);
        for _ in 0..16 {
            x.push(2);
            x.push(4);
            x.push(6);
            x.push(4);
        }
        let mut cic = CIC::new(5, 8);
        let y = cic.process(&x);
        assert_eq!(y, vec!{0i16, 1, 3, 3, 4, 4, 4, 4});
    }
}
