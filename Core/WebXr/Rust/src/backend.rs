//! The platform tracker contract. ARKit, ARCore and OpenXR implement this; the core never
//! talks to a platform SDK directly. Everything here is synchronous and called from the
//! session's frame task: the backend signals frame readiness through the host's own
//! completion mechanism (the N-API dispatcher or a WASI pollable), never by polling.

use crate::features::Feature;
use crate::space::RigidTransform;

/// A viewer pose for one frame: eye views in the local reference space.
#[derive(Clone, Debug, PartialEq)]
pub struct ViewerPose {
    pub transform: RigidTransform,
    pub views: Vec<View>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct View {
    pub transform: RigidTransform,
    pub projection: [f32; 16],
    pub viewport: [u32; 4],
}

pub trait Backend {
    /// Features this device can enable for the requested mode; the core intersects it with the request.
    fn supported_features(&self) -> &[Feature];
    /// The viewer pose for the frame being started, or `None` while tracking is lost.
    fn viewer_pose(&mut self) -> Option<ViewerPose>;
}
