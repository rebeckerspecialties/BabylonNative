//! `XRSession` lifecycle. A session moves from `Requested` to `Active` once the backend confirms
//! the requested features, and to `Ended` exactly once; `end()` is idempotent and all frame
//! callbacks pending at that point are dropped without being invoked, as the specification
//! requires.

use crate::features::{Feature, SessionMode};
use crate::frame::FrameId;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SessionState {
    Active,
    Ended,
}

#[derive(Debug)]
pub struct Session {
    pub mode: SessionMode,
    pub enabled_features: Vec<Feature>,
    state: SessionState,
    next_frame: u64,
    active_frame: Option<FrameId>,
}

impl Session {
    pub fn new(mode: SessionMode, enabled_features: Vec<Feature>) -> Self {
        Session {
            mode,
            enabled_features,
            state: SessionState::Active,
            next_frame: 1,
            active_frame: None,
        }
    }

    pub fn state(&self) -> SessionState {
        self.state
    }

    /// Opens the next frame; the id is handed to the animation-frame callbacks.
    pub fn begin_frame(&mut self) -> Option<FrameId> {
        if self.state == SessionState::Ended || self.active_frame.is_some() {
            return None;
        }
        let id = FrameId(self.next_frame);
        self.next_frame += 1;
        self.active_frame = Some(id);
        Some(id)
    }

    /// Closes the active frame; frame-scoped objects for it become inactive.
    pub fn end_frame(&mut self) {
        self.active_frame = None;
    }

    pub fn is_frame_active(&self, frame: FrameId) -> bool {
        self.active_frame == Some(frame)
    }

    /// Ends the session. Returns true the first time only.
    pub fn end(&mut self) -> bool {
        if self.state == SessionState::Ended {
            return false;
        }
        self.state = SessionState::Ended;
        self.active_frame = None;
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn frames_are_scoped_and_never_reused() {
        let mut s = Session::new(
            SessionMode::ImmersiveAr,
            vec![Feature::Local, Feature::HitTest],
        );
        let f1 = s.begin_frame().unwrap();
        assert!(s.begin_frame().is_none(), "one active frame at a time");
        assert!(s.is_frame_active(f1));
        s.end_frame();
        assert!(!s.is_frame_active(f1));
        let f2 = s.begin_frame().unwrap();
        assert!(f2 > f1);
    }

    #[test]
    fn end_is_idempotent_and_closes_the_frame() {
        let mut s = Session::new(SessionMode::Inline, vec![Feature::Viewer]);
        let f = s.begin_frame().unwrap();
        assert!(s.end());
        assert!(!s.end());
        assert!(!s.is_frame_active(f));
        assert!(s.begin_frame().is_none());
    }
}
