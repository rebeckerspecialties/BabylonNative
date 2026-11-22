/// A failed acquisition must not consume the pending image update.
#[derive(Default)]
pub(crate) struct PresentationDamage {
    pending: bool,
}

impl PresentationDamage {
    pub(crate) fn invalidate(&mut self) {
        self.pending = true;
    }

    pub(crate) fn is_pending(&self) -> bool {
        self.pending
    }

    pub(crate) fn presented(&mut self) {
        self.pending = false;
    }
}

#[cfg(test)]
mod tests {
    use super::PresentationDamage;

    #[test]
    fn idle_ticks_do_not_request_a_copy() {
        let damage = PresentationDamage::default();
        for _ in 0..100 {
            assert!(!damage.is_pending());
        }
    }

    #[test]
    fn coalesces_updates_and_only_consumes_them_after_presentation() {
        let mut damage = PresentationDamage::default();
        for count in [1, 2, 100] {
            for _ in 0..count {
                damage.invalidate();
            }
            for _ in 0..3 {
                // Surface timeout/occlusion leaves the update available for retry.
                assert!(damage.is_pending());
            }
            damage.presented();
            assert!(!damage.is_pending());
        }
    }

    #[test]
    fn independent_contexts_and_resize_after_present() {
        let mut first = PresentationDamage::default();
        let second = PresentationDamage::default();
        first.invalidate();
        first.presented();
        first.invalidate();
        assert!(first.is_pending());
        assert!(!second.is_pending());
    }
}
