//! WebXR core shared by both bindings of this repository family:
//!
//! - BabylonNative's NativeXr plugin (N-API, C++ glue over the C ABI of this crate), and
//! - the `webidl:webxr` WIT world generated from the same W3C IDL (WASI components).
//!
//! The crate owns the specification-level state machine (session lifecycle, reference
//! spaces and poses, frame scoping, layers and sub-images, hit tests, anchors, planes and
//! meshes, lighting and depth estimates). Platform trackers (ARKit, ARCore, OpenXR) stay
//! outside and feed it through [`backend::Backend`]. Rendering resources are referred to
//! by the numeric handle ids the WebGPU command stream already uses, so a projection
//! layer's textures are the same objects on both bindings.

pub mod backend;
pub mod features;
pub mod ffi;
pub mod frame;
pub mod session;
pub mod space;
