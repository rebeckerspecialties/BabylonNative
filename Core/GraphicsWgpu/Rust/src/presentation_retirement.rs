/// Keeps two newer presentations available while retiring the oldest one.
/// Stored per runtime, never shared across devices or queues.
pub(crate) struct PresentationRetirement<T> {
    older: Option<T>,
    newer: Option<T>,
}

impl<T> PresentationRetirement<T> {
    pub(crate) fn new() -> Self {
        Self {
            older: None,
            newer: None,
        }
    }

    pub(crate) fn submit(&mut self, submission: T) -> Option<T> {
        let oldest = self.older.take();
        self.older = self.newer.replace(submission);
        oldest
    }
}

#[cfg(test)]
mod tests {
    use super::PresentationRetirement;

    #[test]
    fn empty_queue_and_first_two_presentations_do_not_wait() {
        let mut queue = PresentationRetirement::new();
        assert!(queue.older.is_none() && queue.newer.is_none());
        assert_eq!(queue.submit(10), None);
        assert_eq!(queue.submit(20), None);
    }

    #[test]
    fn retires_every_older_submission_once_without_draining_newer_work() {
        let mut queue = PresentationRetirement::new();
        for submission in 0usize..100 {
            assert_eq!(queue.submit(submission), submission.checked_sub(2));
            assert_eq!(queue.newer, Some(submission));
            assert_eq!(queue.older, submission.checked_sub(1));
        }
    }

    #[test]
    fn independent_runtimes_do_not_share_submission_indices() {
        let mut first = PresentationRetirement::new();
        let mut second = PresentationRetirement::new();
        first.submit(1);
        first.submit(2);
        assert_eq!(second.submit(100), None);
        assert_eq!(first.submit(3), Some(1));
        assert_eq!(second.submit(200), None);
        assert_eq!(second.submit(300), Some(100));
    }
}
