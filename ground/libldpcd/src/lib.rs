mod parity_check_matrix;
use parity_check_matrix::{N, K, is_codeword};

/// Threshold and pack `llrs` into a K-long binary message
fn pack_message(llrs: &[f64; N]) -> [u8; K/8] {
    let mut message = [0u8; K/8];
    for a in 0usize..N/2 {
        if llrs[a] <= 0.0 {
            message[a/8] |= 1 << (7 - (a % 8));
        }
    }
    message
}

/// Decode from N `llrs` into a packed K-long message
///
/// Each llr is a log likelihood ratio with positive values more likely to be 0.
///
/// Returns the corresponding message bytes if decoding was possible, or None.
pub fn decode(llrs: &[f64; N]) -> Option<[u8; K/8]> {
    const MAX_ITERS: u32 = 100;
    let h = parity_check_matrix::h();
    let (conns_chk, conns_var) = parity_check_matrix::connections(&h);
    let mut r = [0.0f64; N];
    let mut u = [[0.0f64; N]; K];
    let mut v = [[0.0f64; K]; N];

    // Initialise r_a, v_(a->i) = llrs[a]
    for a in 0..N {
        r[a] = llrs[a];
        for v_ai in v[a].iter_mut() {
            *v_ai = llrs[a];
        }
    }

    for _ in 0..MAX_ITERS {
        // Check if we have a codeword yet
        if is_codeword(&r, &h) {
            return Some(pack_message(&r));
        }

        // Compute check node to variable node messages
        // u_(i->a) = 2 atanh(product_[b!=a](tanh( v_(b->i) / 2)))
        for i in 0..K {
            for &a in &conns_chk[i] {
                u[i][a] = 1.0;
                for &b in &conns_chk[i] {
                    if b != a {
                        // This one line is responsible for 99% of CPU use at
                        // low SNR. Could replace with a min* algorithm...
                        u[i][a] *= (v[b][i] / 2.0).tanh();
                    }
                }
                u[i][a] = 2.0 * u[i][a].atanh();
            }
        }

        for a in 0..N {
            // Compute variable nodes to check nodes
            // v_(a->i) = llrs[a] + sum_[j!=i](u_(j->a))
            for &i in &conns_var[a] {
                v[a][i] = llrs[a];
                for &j in &conns_var[a] {
                    if j != i {
                        v[a][i] += u[j][a];
                    }
                }
            }

            // Compute current marginals
            r[a] = llrs[a];
            for i in 0..K {
                r[a] += u[i][a];
            }
        }
    }

    None
}

#[no_mangle]
pub extern fn ldpc_decode(llrs: &[f64; N], out: &mut[u8; K/8]) -> i32 {
    match decode(&llrs) {
        Some(result) => { *out = result; return 0; },
        None => { return 1; }
    }
}

#[cfg(test)]
mod tests {
    use super::{pack_message, decode};
    use parity_check_matrix::N;

    #[test]
    fn test_decode() {
        let cw: [f64; N] = [
            -1.76,  5.15, -5.56,  3.19,  6.52, -8.33, -3.89, -2.14,  7.46,
             7.49,  3.73, -5.79,  2.09, -4.05,  8.58, -4.85,  8.08, -5.77,
            -8.14,  6.42,  2.40, -4.82, -5.01,  3.14,  3.43,  7.02,  1.74,
             4.59,  4.97,  4.84,  7.33, -6.24,  6.40, -6.15,  6.53,  1.86,
             2.29,  3.31,  4.02,  6.54,  5.23,  7.05,  8.08,  2.26, -2.50,
            -4.50,  4.13,  3.82,  9.59,  4.43,  3.46,  5.10,  3.41,  4.71,
            -3.43,  8.50,  2.02, -6.70, -3.63, -4.16, -5.09,  6.05,  4.87,
            -4.25, -2.64, -2.75,  3.97,  8.61,  4.86, -6.29,  3.66, -8.36,
             2.11, -5.20,  3.95,  4.53,  5.06, -4.25, -2.88, -6.35,  2.33,
            -6.33,  1.82,  7.68,  6.14, -5.48, -4.22, -8.44, -3.11,  4.59,
            -8.47,  6.24,  6.47,  7.17,  7.67,  2.84, -2.52,  7.58, -5.87,
             4.35,  5.99,  1.35,  4.02,  5.43,  3.66, -4.25, -8.79, -3.61,
            -4.72, -5.11, -5.92, -8.41,  1.73, -7.37,  5.80,  2.74, -4.67,
             2.36,  6.08,  6.23, -9.03, -5.23, -5.64, -2.47,  5.78, -2.76,
            -3.12,  3.10,  5.79,  8.55,  3.95, -7.06, -7.45, -2.13, -6.30,
             4.95,  5.90,  7.34, -4.69,  5.91,  7.32,  4.00,  6.14, -1.19,
             8.65, -4.77,  8.62, -7.81, -5.34,  3.98, -5.19, -5.91, -3.96,
             3.10, -8.63,  1.52, -6.79,  4.63, -6.90,  7.23, -6.97, -0.84,
             6.87, -7.43,  7.89,  5.81, -4.27,  1.40, -3.35,  2.74, -4.65,
            -6.23, -2.70, -4.56, -1.41, -1.83,  4.91, -3.34,  9.12,  6.38,
             6.04,  1.38, -6.29, -5.84, -6.34, -4.95,  5.07, -2.35, -1.81,
            -5.68, -4.04, -4.11,  7.50,  5.93, -4.09,  7.38, -5.54, -6.14,
            -6.28,  9.69,  7.00, -6.76,  2.98, -6.40,  3.04, -5.62,  5.34,
             6.59, -6.79, -5.40, -1.46, -4.66, -8.08, -5.90, -6.74,  7.94,
             5.77,  3.18,  2.34, -0.80, -2.29,  7.52,  3.62,  4.77, -5.13,
            -9.08, -8.20,  5.23,  4.35, -2.79, -6.45,  6.75,  6.87, -9.38,
             2.96,  5.20, -5.61,  4.79, -5.59,  5.47,  5.87,  4.17,  3.62,
            -2.49, -6.61,  2.19, -3.07,  4.16,  6.06, -1.97, -8.25,  4.42,
            -1.07, -5.57, -4.65, -4.74];
        let result = decode(&cw).unwrap();
        assert!(result == [
            0xA7, 0x15, 0x66, 0x01, 0x40, 0x0C, 0x02, 0x79, 0xC5, 0x47, 0x47, 0xA0,
            0xA0, 0x7F, 0x48, 0xF6]);

        let bad_cw: [f64; N] = [
             0.58,  0.96,  0.06, -0.68, -1.05, -2.38, -0.59,  1.87,  7.43,
            -1.90, -1.32, -0.32,  4.89,  0.35,  1.37, -1.30,  0.83, -3.13,
            -3.47,  0.20,  2.30, -0.48, -2.63, -0.72,  0.86,  1.58,  3.79,
             2.79, -3.16,  2.47,  5.25, -0.95, -1.18,  2.23,  2.05, -3.41,
            -3.03, -3.51, -4.26, -0.43, -0.54, -1.93,  2.30,  0.62, -2.05,
             5.27,  2.01, -1.29,  2.72,  1.34, -1.37,  2.31,  2.66,  1.52,
            -0.05, -2.11,  2.21, -4.75, -3.83,  3.17,  1.79,  2.05,  2.08,
             5.95, -1.08,  2.28, -1.73, -0.46, -3.96, -3.62,  3.82, -0.12,
            -3.08,  1.60, -5.36, -4.42,  2.51,  0.62, -2.68,  3.93,  0.43,
             6.12, -3.53,  1.01, -2.47, -1.73, -3.20,  3.63, -1.02,  2.66,
             1.75,  3.32, -6.39, -0.66,  2.72,  0.60, -0.27,  3.18,  1.24,
             4.74, -1.83,  1.77, -1.54,  0.79,  1.15, -3.36,  0.66,  0.14,
             3.44,  1.76,  4.80, -2.09,  2.30, -0.76,  2.03, -1.11, -0.57,
             1.29,  0.20, -2.28, -0.20,  2.26, -2.53, -0.10, -2.47,  0.32,
             1.61, -1.75, -4.39, -2.80,  3.17,  4.76, -2.17, -0.75, -5.51,
            -1.21, -2.21, -3.34, -1.87,  1.39, -1.78, -1.54,  0.05, -2.28,
            -2.96,  0.68, -0.96, -2.34,  0.41, -1.57,  4.87, -0.07, -1.83,
            -1.14,  3.86,  1.86,  1.32,  4.32,  0.87, -2.54,  0.75,  1.21,
             1.07,  1.45,  0.13, -0.43, -0.78,  2.20,  2.16, -0.14, -2.89,
             2.23, -0.37, -1.58, -3.80,  0.09, -4.33, -2.53, -0.51, -3.12,
            -4.38, -3.98, -1.98, -1.71,  4.35, -1.62,  2.72, -5.42, -0.05,
            -1.08, -0.70,  0.91,  6.22, -1.73,  0.38, -3.06, -4.74,  0.76,
             2.33, -2.96, -2.53, -1.31, -1.10, -9.21, -3.28, -2.60,  4.56,
             1.82,  0.02, -0.07,  4.06, -4.85, -0.49,  0.34,  0.69, -2.47,
            -2.00,  0.47, -3.81, -2.54,  3.56,  2.63,  1.28,  1.79,  1.70,
            -3.01, -1.67, -1.14,  5.00,  2.32, -3.40, -0.49, -6.99, -4.32,
             8.28, -2.92, -3.82,  1.90,  1.98,  2.00,  1.01,  0.65,  1.59,
             0.39, -0.04,  3.09,  1.50, -5.81, -0.70, -6.44,  5.38,  0.38,
             3.62, -0.35,  2.04,  0.35
        ];
        let result = decode(&bad_cw);
        assert!(result == None);
    }

    #[test]
    fn test_pack_message() {
        let x: [f64; N] = [
            -1.0,  1.0, -1.0,  1.0,  1.0, -1.0, -1.0, -1.0,  1.0,  1.0,  1.0, -1.0,
             1.0, -1.0,  1.0, -1.0,  1.0, -1.0, -1.0,  1.0,  1.0, -1.0, -1.0,  1.0,
             1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0, -1.0,  1.0, -1.0,  1.0,  1.0,
             1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0, -1.0, -1.0,  1.0,  1.0,
             1.0,  1.0,  1.0,  1.0,  1.0,  1.0, -1.0,  1.0,  1.0, -1.0, -1.0, -1.0,
            -1.0,  1.0,  1.0, -1.0, -1.0, -1.0,  1.0,  1.0,  1.0, -1.0,  1.0, -1.0,
             1.0, -1.0,  1.0,  1.0,  1.0, -1.0, -1.0, -1.0,  1.0, -1.0,  1.0,  1.0,
             1.0, -1.0, -1.0, -1.0, -1.0,  1.0, -1.0,  1.0,  1.0,  1.0,  1.0,  1.0,
            -1.0,  1.0, -1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0, -1.0, -1.0, -1.0,
            -1.0, -1.0, -1.0, -1.0,  1.0, -1.0,  1.0,  1.0, -1.0,  1.0,  1.0,  1.0,
            -1.0, -1.0, -1.0, -1.0,  1.0, -1.0, -1.0,  1.0,  1.0,  1.0,  1.0, -1.0,
            -1.0, -1.0, -1.0,  1.0,  1.0,  1.0, -1.0,  1.0,  1.0,  1.0,  1.0, -1.0,
             1.0, -1.0,  1.0, -1.0, -1.0,  1.0, -1.0, -1.0, -1.0,  1.0, -1.0,  1.0,
            -1.0,  1.0, -1.0,  1.0, -1.0, -1.0,  1.0, -1.0,  1.0,  1.0, -1.0,  1.0,
            -1.0,  1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0,  1.0, -1.0,  1.0,  1.0,
             1.0,  1.0, -1.0, -1.0, -1.0, -1.0,  1.0, -1.0, -1.0, -1.0, -1.0, -1.0,
             1.0,  1.0, -1.0,  1.0, -1.0, -1.0, -1.0,  1.0,  1.0, -1.0,  1.0, -1.0,
             1.0, -1.0,  1.0,  1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0,  1.0,
             1.0,  1.0,  1.0, -1.0, -1.0,  1.0,  1.0,  1.0, -1.0, -1.0, -1.0,  1.0,
             1.0, -1.0, -1.0,  1.0,  1.0, -1.0,  1.0,  1.0, -1.0,  1.0, -1.0,  1.0,
             1.0,  1.0,  1.0, -1.0, -1.0,  1.0, -1.0,  1.0,  1.0, -1.0, -1.0,  1.0,
            -1.0, -1.0, -1.0, -1.0
        ];
        let y = pack_message(&x);
        assert!(y == [
            0xA7, 0x15, 0x66, 0x01, 0x40, 0x0C, 0x02, 0x79, 0xC5, 0x47, 0x47, 0xA0,
            0xA0, 0x7F, 0x48, 0xF6]);

    }
}
