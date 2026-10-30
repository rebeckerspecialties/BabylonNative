//! The no-DOM profile over the vendored WebXR IDL set: what must disappear, what must survive.

use std::path::PathBuf;

use webxr_idl_gen::{dom_profile::Profile, generate, validate};

fn idl_dir() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../Core/WebXr/idl")
}

fn deps_dir() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../Core/WebXr/wit/deps")
}

fn generated_no_dom() -> webxr_idl_gen::Generated {
    generate(&idl_dir(), Some(&Profile::no_dom())).expect("conversion succeeds")
}

#[test]
fn removes_dom_html_webgl_and_permissions_surfaces() {
    let generated = generated_no_dom();
    let wit = generated.wit.to_lowercase();
    for forbidden in [
        "event-target",
        "resource event ",
        "resource element",
        "resource navigator",
        "html-canvas-element",
        "html-video-element",
        "offscreen-canvas",
        "web-gl",
        "webgl",
        "dom-overlay",
        "permission",
    ] {
        assert!(
            !wit.contains(forbidden),
            "{forbidden:?} must not appear in the no-DOM WIT"
        );
    }
}

#[test]
fn keeps_the_webgpu_xr_surface_and_pollable_events() {
    let generated = generated_no_dom();
    let wit = &generated.wit;
    for required in [
        "resource xr-system {",
        "resource xr-session {",
        "resource xr-frame {",
        "resource xr-reference-space {",
        "resource xr-projection-layer {",
        "resource xrgpu-binding {",
        "resource xrgpu-sub-image {",
        "color-texture: func() -> gpu-texture;",
        "request-reference-space: func(",
        "request-animation-frame: func(",
        "onend-subscribe: func() -> pollable;",
        "onselect-subscribe: func() -> pollable;",
        "enum xr-session-mode {",
        "resource xr-hit-test-source {",
        "resource xr-anchor {",
        "resource xr-light-estimate {",
        "resource xrcpu-depth-information {",
        "resource xr-plane {",
    ] {
        assert!(
            wit.contains(required),
            "{required:?} must survive the no-DOM profile"
        );
    }
}

#[test]
fn reports_what_it_removed() {
    let generated = generated_no_dom();
    let report = generated.profile_report.expect("profile report");
    for definition in [
        "EventTarget",
        "XRWebGLLayer",
        "XRWebGLBinding",
        "XRDOMOverlayState",
        "XRPermissionStatus",
        "Navigator",
    ] {
        assert!(
            report
                .dropped_definitions
                .iter()
                .any(|name| name == definition),
            "{definition} should be dropped"
        );
    }
    // XRSession loses the DOM overlay state; GPUCanvasContext loses its canvas accessor.
    assert!(report
        .dropped_members
        .contains(&("XRSession".into(), "domOverlayState".into())));
    assert!(report
        .dropped_members
        .contains(&("GPUCanvasContext".into(), "canvas".into())));
    // Event-derived interfaces become roots instead of inheriting from the DOM.
    for owner in ["XRSystem", "XRSession", "XRSpace", "XRLayer"] {
        assert!(
            report.cleared_inheritance.iter().any(|name| name == owner),
            "{owner} inheritance should be cleared"
        );
    }
    assert_eq!(
        generated.placeholder_callbacks,
        vec!["XRFrameRequestCallback".to_string()]
    );
}

#[test]
fn no_dom_output_resolves_against_wasi_io() {
    let generated = generated_no_dom();
    validate(&generated.wit, &deps_dir()).expect("WIT resolves");
}

#[test]
fn full_profile_still_converts() {
    // Without a profile the DOM surface is present; this guards the conversion itself.
    let generated = generate(&idl_dir(), None).expect("conversion succeeds");
    assert!(generated.wit.contains("resource event-target"));
    assert!(generated.wit.contains("resource xr-web-gl-layer {"));
}
