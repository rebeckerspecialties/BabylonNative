// Record at encode time; apply only on submission, in submission order.
#[derive(Clone, Copy, Default)]
pub(crate) struct Writes {
    ids: [u64; 2],
    touched: [u32; 2],
    stored: [u32; 2],
}

impl Writes {
    pub(crate) fn new(motion: u64, depth: u64) -> Self {
        Self { ids: [motion, depth], ..Self::default() }
    }

    pub(crate) fn record(&mut self, id: u64, base: u32, views: u32, store: bool) {
        if let Some(i) = self.ids.iter().position(|&wanted| wanted == id) {
            let mask = views.checked_shl(base).unwrap_or(0);
            self.touched[i] |= mask;
            self.stored[i] = (self.stored[i] & !mask) | if store { mask } else { 0 };
        }
    }

    pub(crate) fn any(&self) -> bool { self.touched != [0, 0] }
    pub(crate) fn empty_command(&self) -> Self { Self::new(self.ids[0], self.ids[1]) }

    pub(crate) fn submit(&mut self, command: &Self) {
        if self.ids != command.ids { return; }
        for i in 0..2 {
            self.stored[i] = (self.stored[i] & !command.touched[i]) | command.stored[i];
        }
    }

    pub(crate) fn complete(&self, views: u32) -> bool {
        (1..=32).contains(&views) && self.stored.iter().all(|&v| v.trailing_ones() >= views)
    }
}

#[cfg(test)]
mod tests {
    use super::Writes;
    #[test]
    fn only_submitted_stores_for_both_eyes_and_attachments_count() {
        let mut frame = Writes::new(3, 5);
        let mut left = Writes::new(3, 5);
        left.record(3, 0, 1, true);
        left.record(5, 0, 1, true);
        assert!(!frame.complete(1));
        frame.submit(&left);
        assert!(frame.complete(1));
        assert!(!frame.complete(2));
        let mut right = Writes::new(3, 5);
        right.record(3, 1, 1, true);
        frame.submit(&right);
        assert!(!frame.complete(2));
        right.record(5, 1, 1, true);
        frame.submit(&right);
        assert!(frame.complete(2));
    }
    #[test]
    fn late_old_buffers_discards_and_unrelated_targets_cannot_qualify_a_frame() {
        let mut frame = Writes::new(11, 12);
        let mut old = Writes::new(9, 10);
        old.record(9, 0, 3, true);
        old.record(10, 0, 3, true);
        frame.submit(&old);
        let mut cmd = Writes::new(11, 12);
        cmd.record(100, 0, 3, true);
        assert!(!cmd.any() && !frame.complete(2));
        cmd.record(11, 0, 3, true);
        cmd.record(12, 0, 3, true);
        cmd.record(12, 1, 1, false);
        frame.submit(&cmd);
        assert!(frame.complete(1) && !frame.complete(2));
        cmd.record(12, 1, 1, true);
        frame.submit(&cmd);
        assert!(frame.complete(2) && !frame.complete(0) && !frame.complete(33));
    }
}
