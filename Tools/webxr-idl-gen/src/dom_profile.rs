//! The "no DOM" profile. Neither deployment target has a document object model: the snqr
//! runtime is a bespoke WebAssembly host and BabylonNative is a JavaScript runtime with W3C
//! graphics APIs but no browser plumbing. WebXR is specified against the DOM, so before the
//! IDL is converted this pass removes what cannot exist there and records what it removed.
//!
//! Rules, applied to the parsed definitions:
//! 1. Definitions named in [`Profile::dropped`] are removed (interfaces, dictionaries, enums,
//!    typedefs, mixins, their partials and `includes` statements). Typedefs whose type refers
//!    to a dropped name are dropped too, and their names join the set, until nothing changes.
//! 2. Members of the remaining interfaces, mixins and dictionaries are removed when their
//!    attribute type, return type or any argument type refers to a dropped name.
//! 3. Inheritance from a dropped parent is cleared (`XRSession : EventTarget` becomes a root
//!    resource). `EventHandler` attributes are kept: the converter maps each one to a
//!    `<name>-subscribe: func() -> pollable`, which is the event model both hosts implement.

use std::collections::HashSet;

use weedle::argument::Argument;
use weedle::interface::InterfaceMember;
use weedle::mixin::MixinMember;
use weedle::types::{NonAnyType, ReturnType, SingleType, Type, UnionMemberType};
use weedle::Definition;

#[derive(Clone, Debug)]
pub struct Profile {
    pub dropped: HashSet<String>,
}

impl Profile {
    /// DOM, HTML media, WebGL and Permissions types: no implementation exists in either host.
    pub fn no_dom() -> Self {
        const NAMES: &[&str] = &[
            // DOM and HTML
            "Event",
            "EventInit",
            "EventTarget",
            "Element",
            "Navigator",
            "WorkerNavigator",
            "GlobalEventHandlers",
            "HTMLCanvasElement",
            "HTMLImageElement",
            "HTMLVideoElement",
            "ImageBitmap",
            "ImageData",
            "OffscreenCanvas",
            "VideoFrame",
            // Permissions API: the host grants camera and tracking access itself.
            "PermissionDescriptor",
            "PermissionStatus",
            "XRPermissionDescriptor",
            "XRPermissionStatus",
            "XRSessionSupportedPermissionDescriptor",
            // WebXR DOM Overlays module: needs a document.
            "XRDOMOverlayInit",
            "XRDOMOverlayState",
            "XRDOMOverlayType",
            // WebGL: both hosts render through WebGPU only.
            "WebGLRenderingContext",
            "WebGL2RenderingContext",
            "WebGLRenderingContextBase",
            "WebGLContextAttributes",
            "WebGLTexture",
            "WebGLFramebuffer",
            "XRWebGLRenderingContext",
            "XRWebGLLayerInit",
            "XRWebGLLayer",
            "XRWebGLBinding",
            "XRWebGLSubImage",
            "XRWebGLDepthInformation",
        ];
        Profile {
            dropped: NAMES.iter().map(|name| (*name).to_owned()).collect(),
        }
    }

    fn drops(&self, name: &str) -> bool {
        self.dropped.contains(name)
    }
}

/// What a profile removed, for logs and tests.
#[derive(Debug, Default)]
pub struct Report {
    pub dropped_definitions: Vec<String>,
    /// `(owner, member)` pairs.
    pub dropped_members: Vec<(String, String)>,
    pub cleared_inheritance: Vec<String>,
}

pub fn apply<'a>(profile: &Profile, definitions: &mut Vec<Definition<'a>>) -> Report {
    let mut profile = profile.clone();
    let mut report = Report::default();

    // Rule 1b: typedefs built on dropped types are dropped and widen the set, to a fixpoint.
    loop {
        let mut widened = false;
        for definition in definitions.iter() {
            if let Definition::Typedef(typedef) = definition {
                let name = typedef.identifier.0;
                if !profile.drops(name) && mentions(&typedef.type_.type_, &profile) {
                    profile.dropped.insert(name.to_owned());
                    widened = true;
                }
            }
        }
        if !widened {
            break;
        }
    }

    // Rule 1: whole definitions.
    definitions.retain(|definition| {
        let dropped_name = match definition {
            Definition::Interface(d) => profile.drops(d.identifier.0).then_some(d.identifier.0),
            Definition::PartialInterface(d) => {
                profile.drops(d.identifier.0).then_some(d.identifier.0)
            }
            Definition::InterfaceMixin(d) => {
                profile.drops(d.identifier.0).then_some(d.identifier.0)
            }
            Definition::PartialInterfaceMixin(d) => {
                profile.drops(d.identifier.0).then_some(d.identifier.0)
            }
            Definition::Dictionary(d) => profile.drops(d.identifier.0).then_some(d.identifier.0),
            Definition::PartialDictionary(d) => {
                profile.drops(d.identifier.0).then_some(d.identifier.0)
            }
            Definition::Enum(d) => profile.drops(d.identifier.0).then_some(d.identifier.0),
            Definition::Typedef(d) => profile.drops(d.identifier.0).then_some(d.identifier.0),
            Definition::CallbackInterface(d) => {
                profile.drops(d.identifier.0).then_some(d.identifier.0)
            }
            Definition::Namespace(d) => profile.drops(d.identifier.0).then_some(d.identifier.0),
            Definition::IncludesStatement(d) => (profile.drops(d.lhs_identifier.0)
                || profile.drops(d.rhs_identifier.0))
            .then_some(d.lhs_identifier.0),
            _ => None,
        };
        match dropped_name {
            Some(name) => {
                report.dropped_definitions.push(name.to_owned());
                false
            }
            None => true,
        }
    });

    // Rules 2 and 3: members and inheritance of what remains.
    for definition in definitions.iter_mut() {
        match definition {
            Definition::Interface(d) => {
                let owner = d.identifier.0;
                if let Some(parent) = &d.inheritance {
                    if profile.drops(parent.identifier.0) {
                        d.inheritance = None;
                        report.cleared_inheritance.push(owner.to_owned());
                    }
                }
                retain_interface_members(owner, &mut d.members.body, &profile, &mut report);
            }
            Definition::PartialInterface(d) => {
                let owner = d.identifier.0;
                retain_interface_members(owner, &mut d.members.body, &profile, &mut report);
            }
            Definition::InterfaceMixin(d) => {
                let owner = d.identifier.0;
                retain_mixin_members(owner, &mut d.members.body, &profile, &mut report);
            }
            Definition::PartialInterfaceMixin(d) => {
                let owner = d.identifier.0;
                retain_mixin_members(owner, &mut d.members.body, &profile, &mut report);
            }
            Definition::Dictionary(d) => {
                let owner = d.identifier.0;
                if let Some(parent) = &d.inheritance {
                    if profile.drops(parent.identifier.0) {
                        d.inheritance = None;
                        report.cleared_inheritance.push(owner.to_owned());
                    }
                }
                d.members.body.retain(|member| {
                    let keep = !mentions(&member.type_, &profile);
                    if !keep {
                        report
                            .dropped_members
                            .push((owner.to_owned(), member.identifier.0.to_owned()));
                    }
                    keep
                });
            }
            Definition::PartialDictionary(d) => {
                let owner = d.identifier.0;
                d.members.body.retain(|member| {
                    let keep = !mentions(&member.type_, &profile);
                    if !keep {
                        report
                            .dropped_members
                            .push((owner.to_owned(), member.identifier.0.to_owned()));
                    }
                    keep
                });
            }
            _ => {}
        }
    }

    report
}

fn retain_interface_members<'a>(
    owner: &str,
    members: &mut Vec<InterfaceMember<'a>>,
    profile: &Profile,
    report: &mut Report,
) {
    members.retain(|member| {
        let (keep, name) = match member {
            InterfaceMember::Attribute(attribute) => (
                !mentions(&attribute.type_.type_, profile),
                attribute.identifier.0.to_owned(),
            ),
            InterfaceMember::Operation(operation) => (
                !return_mentions(&operation.return_type, profile)
                    && !arguments_mention(&operation.args.body.list, profile),
                operation
                    .identifier
                    .map(|identifier| identifier.0.to_owned())
                    .unwrap_or_else(|| "<special operation>".to_owned()),
            ),
            InterfaceMember::Constructor(constructor) => (
                !arguments_mention(&constructor.args.body.list, profile),
                "constructor".to_owned(),
            ),
            _ => (true, String::new()),
        };
        if !keep {
            report.dropped_members.push((owner.to_owned(), name));
        }
        keep
    });
}

fn retain_mixin_members<'a>(
    owner: &str,
    members: &mut Vec<MixinMember<'a>>,
    profile: &Profile,
    report: &mut Report,
) {
    members.retain(|member| {
        let (keep, name) = match member {
            MixinMember::Attribute(attribute) => (
                !mentions(&attribute.type_.type_, profile),
                attribute.identifier.0.to_owned(),
            ),
            MixinMember::Operation(operation) => (
                !return_mentions(&operation.return_type, profile)
                    && !arguments_mention(&operation.args.body.list, profile),
                operation
                    .identifier
                    .map(|identifier| identifier.0.to_owned())
                    .unwrap_or_else(|| "<special operation>".to_owned()),
            ),
            _ => (true, String::new()),
        };
        if !keep {
            report.dropped_members.push((owner.to_owned(), name));
        }
        keep
    });
}

fn arguments_mention(arguments: &[Argument<'_>], profile: &Profile) -> bool {
    arguments.iter().any(|argument| match argument {
        Argument::Single(single) => mentions(&single.type_.type_, profile),
        Argument::Variadic(variadic) => mentions(&variadic.type_, profile),
    })
}

fn return_mentions(return_type: &ReturnType<'_>, profile: &Profile) -> bool {
    match return_type {
        ReturnType::Undefined(_) => false,
        ReturnType::Type(type_) => mentions(type_, profile),
    }
}

/// True when the type names a dropped definition anywhere in its structure.
pub fn mentions(type_: &Type<'_>, profile: &Profile) -> bool {
    match type_ {
        Type::Single(SingleType::Any(_)) => false,
        Type::Single(SingleType::NonAny(non_any)) => non_any_mentions(non_any, profile),
        Type::Union(union) => union.type_.body.list.iter().any(|member| match member {
            UnionMemberType::Single(single) => non_any_mentions(&single.type_, profile),
            UnionMemberType::Union(nested) => {
                nested.type_.body.list.iter().any(|inner| match inner {
                    UnionMemberType::Single(single) => non_any_mentions(&single.type_, profile),
                    UnionMemberType::Union(_) => false,
                })
            }
        }),
    }
}

fn non_any_mentions(type_: &NonAnyType<'_>, profile: &Profile) -> bool {
    match type_ {
        NonAnyType::Identifier(identifier) => profile.drops(identifier.type_.0),
        NonAnyType::Promise(promise) => return_mentions(&promise.generics.body, profile),
        NonAnyType::Sequence(sequence) => mentions(&sequence.type_.generics.body, profile),
        NonAnyType::FrozenArrayType(array) => mentions(&array.type_.generics.body, profile),
        NonAnyType::ObservableArrayType(array) => mentions(&array.type_.generics.body, profile),
        NonAnyType::RecordType(record) => mentions(&record.type_.generics.body.2, profile),
        _ => false,
    }
}
