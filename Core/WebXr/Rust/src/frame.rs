//! Frame scoping. WebXR objects such as `XRFrame`, `XRViewerPose` and `XRHitTestResult` are only
//! valid during the animation-frame callback they were handed to; the core tracks that with a
//! monotonically increasing frame id, and every frame-scoped accessor checks the id of the frame
//! it belongs to against the session's active frame before answering.

/// Identity of one `XRFrame` within a session. Never reused.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct FrameId(pub u64);

/// Why a frame-scoped accessor refused to answer (`InvalidStateError` in the DOM bindings).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FrameAccessError {
    /// The frame's animation-frame callback has already returned.
    Inactive,
    /// The session that produced the frame has ended.
    SessionEnded,
}
