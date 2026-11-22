/// A complete sample contains one release resolve for each of two different eyes,
/// and at least one eye render targeting each of those same images.
pub(super) fn stereo<K: Copy + Eq>(passes: &[(K, bool)]) -> bool {
    let mut outputs = passes
        .iter()
        .filter_map(|&(id, resolve)| resolve.then_some(id));
    let (Some(left), Some(right), None) = (outputs.next(), outputs.next(), outputs.next()) else {
        return false;
    };
    left != right
        && passes.contains(&(left, false))
        && passes.contains(&(right, false))
        && passes.iter().all(|&(id, _)| id == left || id == right)
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub(super) enum Target {
    Slice(u64, u32),
    StereoArray(u64),
}

pub(super) fn complete(passes: &[(Target, bool)]) -> bool {
    use Target::*;
    if let Some(id) = passes.iter().find_map(|&(target, _)| match target {
        StereoArray(id) => Some(id),
        _ => None,
    }) {
        // A two-layer render is timed once, not once per eye. Match it to the same
        // acquired image's two individual release resolves; reject mixed frames.
        return passes.iter().all(|&(target, resolve)| match target {
            StereoArray(t) => t == id && !resolve,
            Slice(t, layer) => t == id && layer < 2 && resolve,
        }) && (0..2).all(|layer| passes.iter().filter(|&&p| p == (Slice(id, layer), true)).count() == 1);
    }
    stereo(passes)
}

pub(super) fn milliseconds<K>(
    passes: &[(K, bool)],
    bytes: &[u8],
    period_ms: f64,
) -> Option<[f64; 2]> {
    if bytes.len() != passes.len() * 16 || period_ms <= 0. || !period_ms.is_finite() {
        return None;
    }
    let mut sums = [0.; 2];
    for ((_, resolve), pair) in passes.iter().zip(bytes.chunks_exact(16)) {
        let start = u64::from_ne_bytes(pair[..8].try_into().ok()?);
        let end = u64::from_ne_bytes(pair[8..].try_into().ok()?);
        if start == 0 || end <= start {
            return None;
        }
        sums[*resolve as usize] += (end - start) as f64 * period_ms;
    }
    Some(sums)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn layered_pass_is_one_timestamp_pair_covering_both_resolved_slices() {
        use Target::*;
        let draw = (StereoArray(7), false);
        let left = (Slice(7, 0), true);
        let right = (Slice(7, 1), true);
        assert!(complete(&[draw, draw, left, right]));
        assert!(!complete(&[draw, left]));
        assert!(!complete(&[draw, left, right, right]));
        assert!(!complete(&[draw, left, (Slice(8, 1), true)]));
        assert!(!complete(&[draw, (Slice(7, 0), false), left, right]));
        assert!(!complete(&[(StereoArray(7), true), left, right]));
    }
    #[test]
    fn distinct_slices_of_one_image_are_distinct_eyes() {
        let left = (9u64, 0u32);
        let right = (9, 1);
        assert!(stereo(&[
            (left, false),
            (right, false),
            (left, true),
            (right, true)
        ]));
        assert!(!stereo(&[
            (left, false),
            (right, false),
            (left, true),
            (left, true)
        ]));
    }
    #[test]
    fn eye_ids_and_phases_must_match() {
        let valid = [(1, false), (2, false), (1, true), (2, true)];
        assert!(stereo(&valid));
        assert!(!stereo(&valid[..3]));
        assert!(!stereo(&[(1, false), (1, true), (1, true)]));
        assert!(!stereo(&[(1, false), (3, false), (1, true), (2, true)]));
        let mut extra = valid.to_vec();
        extra.push((1, false));
        assert!(stereo(&extra));
        extra.push((3, false));
        assert!(!stereo(&extra));
    }
    #[test]
    fn counts_gpu_time_not_queue_gaps() {
        let bytes: Vec<_> = [1u64, 4, 50, 55, 400, 402, 900, 901]
            .into_iter()
            .flat_map(u64::to_ne_bytes)
            .collect();
        let passes = [(1, false), (2, false), (2, true), (1, true)];
        assert_eq!(milliseconds(&passes, &bytes, 0.5), Some([4., 1.5]));
        assert_eq!(milliseconds(&passes, &bytes[1..], 0.5), None);
        assert_eq!(milliseconds(&passes, &[0; 64], 0.5), None);
        assert_eq!(milliseconds(&passes, &bytes, f64::NAN), None);
    }
}
