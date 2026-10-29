//! Session modes and feature descriptors as the WebXR Device API and its modules define them.
//! The strings are the ones negotiated through `XRSessionInit.requiredFeatures` and
//! `optionalFeatures`; both bindings map their public surface to these values so the set of
//! supported features is the same by construction.

/// `XRSessionMode` (WebXR Device API).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum SessionMode {
    Inline,
    ImmersiveVr,
    ImmersiveAr,
}

impl SessionMode {
    pub fn as_str(self) -> &'static str {
        match self {
            SessionMode::Inline => "inline",
            SessionMode::ImmersiveVr => "immersive-vr",
            SessionMode::ImmersiveAr => "immersive-ar",
        }
    }

    pub fn parse(value: &str) -> Option<Self> {
        match value {
            "inline" => Some(SessionMode::Inline),
            "immersive-vr" => Some(SessionMode::ImmersiveVr),
            "immersive-ar" => Some(SessionMode::ImmersiveAr),
            _ => None,
        }
    }
}

/// Feature descriptors defined by the WebXR Device API and the modules vendored in
/// `Core/WebXr/idl`, in specification order. Unknown descriptors are rejected at session
/// request time, as the specification requires for `requiredFeatures`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Feature {
    // WebXR Device API reference spaces
    Viewer,
    Local,
    LocalFloor,
    BoundedFloor,
    Unbounded,
    // Modules
    Anchors,
    DepthSensing,
    DomOverlay,
    HandTracking,
    HitTest,
    Layers,
    LightEstimation,
    MeshDetection,
    PlaneDetection,
    CameraAccess,
    SecondaryViews,
    WebGpu,
}

impl Feature {
    pub const ALL: [Feature; 17] = [
        Feature::Viewer,
        Feature::Local,
        Feature::LocalFloor,
        Feature::BoundedFloor,
        Feature::Unbounded,
        Feature::Anchors,
        Feature::DepthSensing,
        Feature::DomOverlay,
        Feature::HandTracking,
        Feature::HitTest,
        Feature::Layers,
        Feature::LightEstimation,
        Feature::MeshDetection,
        Feature::PlaneDetection,
        Feature::CameraAccess,
        Feature::SecondaryViews,
        Feature::WebGpu,
    ];

    pub fn as_str(self) -> &'static str {
        match self {
            Feature::Viewer => "viewer",
            Feature::Local => "local",
            Feature::LocalFloor => "local-floor",
            Feature::BoundedFloor => "bounded-floor",
            Feature::Unbounded => "unbounded",
            Feature::Anchors => "anchors",
            Feature::DepthSensing => "depth-sensing",
            Feature::DomOverlay => "dom-overlay",
            Feature::HandTracking => "hand-tracking",
            Feature::HitTest => "hit-test",
            Feature::Layers => "layers",
            Feature::LightEstimation => "light-estimation",
            Feature::MeshDetection => "mesh-detection",
            Feature::PlaneDetection => "plane-detection",
            Feature::CameraAccess => "camera-access",
            Feature::SecondaryViews => "secondary-views",
            Feature::WebGpu => "webgpu",
        }
    }

    pub fn parse(value: &str) -> Option<Self> {
        Feature::ALL
            .iter()
            .copied()
            .find(|feature| feature.as_str() == value)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn feature_descriptors_round_trip() {
        for feature in Feature::ALL {
            assert_eq!(Feature::parse(feature.as_str()), Some(feature));
        }
        assert_eq!(Feature::parse("not-a-feature"), None);
    }

    #[test]
    fn session_modes_round_trip() {
        for mode in [
            SessionMode::Inline,
            SessionMode::ImmersiveVr,
            SessionMode::ImmersiveAr,
        ] {
            assert_eq!(SessionMode::parse(mode.as_str()), Some(mode));
        }
    }
}
