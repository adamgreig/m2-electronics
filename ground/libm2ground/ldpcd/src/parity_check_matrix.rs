//! Defines the parity check matrix in use, and related functionality.

/// Size of the component submatrices of the parity check matrix
const M: usize = 32;

/// Number of blocks per row
const BR: usize = 8;

/// Number of blocks per column
const BC: usize = 4;

/// Code dimension
pub const K: usize = BC * M;

/// Code block length
pub const N: usize = BR * M;

/// Alias for MxM binary matrix
type Block = [[u8; M]; M];

/// Returns an all-zeros array of size M*M
fn z() -> Block {
    return [[0; M]; M];
}

/// Returns the nth right circular shift of the identity matrix of size M*M
fn p(n: usize) -> Block {
    let mut a = z();
    for i in 0usize..M {
        for j in 0usize..M {
            if j == (i + n) % M {
                a[i][j] = 1;
            }
        }
    }

    a
}

/// Returns the identity matrix of size M*M
fn i() -> Block {
    p(0)
}

/// Returns identity_M + phi_M(n) mod 2
fn ip(n: usize) -> Block {
    let mut a = i();
    let b = p(n);
    for i in 0usize..M {
        for j in 0usize..M {
            a[i][j] = (a[i][j] + b[i][j]) % 2
        }
    }

    a
}

/// Store a parity check matrix and its connectivity representation
///
/// TODO: Put N, K etc into here when Associated Constants lands.
pub struct ParityMatrix {
    pub h: [[u8; N]; K],
    pub conns_row: Vec<Vec<usize>>,
    pub conns_col: Vec<Vec<usize>>,
}

impl ParityMatrix {
    /// Compute the parity check matrix
    ///
    /// Parity check matrix is the H_128x256 defined in CCSDS 231.1-O-1 p2-2
    pub fn new() -> ParityMatrix {
        let mut h = [[0u8; N]; K];
        let a = [
            [ip(31),  p(15), p(25), p(0),  z(),   p(20), p(12), i()  ],
            [ p(28), ip(30), p(29), p(24), i(),   z(),   p(1),  p(20)],
            [ p(8),   p(0), ip(28), p(1),  p(29), i(),   z(),   p(21)],
            [ p(18),  p(30), p(0), ip(30), p(25), p(26), i(),   z()  ]
        ];

        for i in 0usize..K {
            for j in 0usize..N {
                h[i][j] = a[i/M][j/M][i % M][j % M];
            }
        }

        let mut conns_row: Vec<Vec<usize>> = Vec::new();
        let mut conns_col: Vec<Vec<usize>> = Vec::new();

        for i in 0usize..K {
            conns_row.push(Vec::new());
            for a in 0usize..N {
                if i == 0 {
                    conns_col.push(Vec::new());
                }

                if h[i][a] == 1 {
                    conns_row[i].push(a);
                    conns_col[a].push(i);
                }
            }
        }

        ParityMatrix{h: h, conns_row: conns_row, conns_col: conns_col}
    }

    /// Compute x.H==0, i.e. "is x a codeword?"
    ///
    /// x is an array of LLRs, >0 implying 0 is more likely
    pub fn is_codeword(&self, x: &[f64; N]) -> bool {
        // For each parity check row
        for i in 0usize..K {
            // Keep track of running parity sum
            let mut parity = 0u32;

            // For each coded bit
            for a in 0usize..N {

                // If this edge is in the parity check matrix and this bit is 1,
                // add it to the parity sum
                if self.h[i][a] == 1 && x[a] <= 0.0 {
                    parity += 1;
                }
            }

            // Break as soon as any parity check row is not satisfied
            if parity % 2 != 0 {
                return false;
            }
        }

        // If we got here without returning, all parity checks were satisfied
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use super::{i, p, ip};

    #[test]
    fn test_i() {
        let ii = i();
        assert!(ii[0][0] == 1);
        assert!(ii[27][27] == 1);
    }

    #[test]
    fn test_p() {
        let pp = p(5);
        assert!(pp[0][5] == 1);
        assert!(pp[27][0] == 1);
    }

    #[test]
    fn test_ip() {
        let ipip = ip(5);
        assert!(ipip[0][0] == 1);
        assert!(ipip[0][5] == 1);
    }

    #[test]
    fn test_h() {
        let pm = ParityMatrix::new();
        let hh90: [u8; N] = [
            0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
            0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
        for a in 0..N {
            assert!(pm.h[90][a] == hh90[a]);
        }
    }
    

    #[test]
    fn test_connections() {
        let pm = ParityMatrix::new();
        assert!(pm.conns_row[10] == [9, 10, 57, 67, 106, 190, 214, 234]);
        assert!(pm.conns_col[90] == [1, 61, 90, 94, 122]);
    }

    #[test]
    fn test_is_codeword() {
        let mut x: [f64; N] = [
            -1.0,  1.0, -1.0,  1.0,  1.0, -1.0, -1.0, -1.0,  1.0,  1.0,  1.0,
            -1.0,  1.0, -1.0,  1.0, -1.0,  1.0, -1.0, -1.0,  1.0,  1.0, -1.0,
            -1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0, -1.0,  1.0,
            -1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0,
            -1.0, -1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0, -1.0,
             1.0,  1.0, -1.0, -1.0, -1.0, -1.0,  1.0,  1.0, -1.0, -1.0, -1.0,
             1.0,  1.0,  1.0, -1.0,  1.0, -1.0,  1.0, -1.0,  1.0,  1.0,  1.0,
            -1.0, -1.0, -1.0,  1.0, -1.0,  1.0,  1.0,  1.0, -1.0, -1.0, -1.0,
            -1.0,  1.0, -1.0,  1.0,  1.0,  1.0,  1.0,  1.0, -1.0,  1.0, -1.0,
             1.0,  1.0,  1.0,  1.0,  1.0,  1.0, -1.0, -1.0, -1.0, -1.0, -1.0,
            -1.0, -1.0,  1.0, -1.0,  1.0,  1.0, -1.0,  1.0,  1.0,  1.0, -1.0,
            -1.0, -1.0, -1.0,  1.0, -1.0, -1.0,  1.0,  1.0,  1.0,  1.0, -1.0,
            -1.0, -1.0, -1.0,  1.0,  1.0,  1.0, -1.0,  1.0,  1.0,  1.0,  1.0,
            -1.0,  1.0, -1.0,  1.0, -1.0, -1.0,  1.0, -1.0, -1.0, -1.0,  1.0,
            -1.0,  1.0, -1.0,  1.0, -1.0,  1.0, -1.0, -1.0,  1.0, -1.0,  1.0,
             1.0, -1.0,  1.0, -1.0,  1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0,
             1.0, -1.0,  1.0,  1.0,  1.0,  1.0, -1.0, -1.0, -1.0, -1.0,  1.0,
            -1.0, -1.0, -1.0, -1.0, -1.0,  1.0,  1.0, -1.0,  1.0, -1.0, -1.0,
            -1.0,  1.0,  1.0, -1.0,  1.0, -1.0,  1.0, -1.0,  1.0,  1.0, -1.0,
            -1.0, -1.0, -1.0, -1.0, -1.0, -1.0,  1.0,  1.0,  1.0,  1.0, -1.0,
            -1.0,  1.0,  1.0,  1.0, -1.0, -1.0, -1.0,  1.0,  1.0, -1.0, -1.0,
             1.0,  1.0, -1.0,  1.0,  1.0, -1.0,  1.0, -1.0,  1.0,  1.0,  1.0,
             1.0, -1.0, -1.0,  1.0, -1.0,  1.0,  1.0, -1.0, -1.0,  1.0, -1.0,
            -1.0, -1.0, -1.0
        ];
        let pm = ParityMatrix::new();
        assert!(pm.is_codeword(&x));
        x[100] = -x[100];
        assert!(!pm.is_codeword(&x));
    }
}
