#include <Babylon/Plugins/NativeXr.h>

#include <Babylon/JsRuntimeScheduler.h>

#include <XR.h>

#include "Constants.h"
#include "PointerEvent.h"
#include "XRWebGLBinding.h"
#include "XRWebGLLayer.h"
#include "XRRigidTransform.h"
#include "XRView.h"
#include "XRViewerPose.h"
#include "XRPose.h"
#include "XRRay.h"
#include "XRReferenceSpace.h"
#include "XRAnchor.h"
#include "XRHitTestSource.h"
#include "XRHitTestResult.h"
#include "XRPlane.h"
#include "XRMesh.h"
#include "XRHand.h"

#include "XRFrame.h"

namespace Babylon
{
    // Using Plugins namespace warning C5046: 'Babylon::`anonymous-namespace'::XRFrame::xxx': Symbol involving type with internal linkage not defined
    namespace Plugins
    {
        void XRFrame::Initialize(Napi::Env env)
        {
            Napi::HandleScope scope{ env };

            Napi::Function func = DefineClass(
                env,
                JS_CLASS_NAME,
                {
                    InstanceMethod("getViewerPose", &XRFrame::GetViewerPose),
                    InstanceMethod("getPose", &XRFrame::GetPose),
                    InstanceMethod("getPoseData", &XRFrame::GetPoseData),
                    InstanceMethod("getHitTestResults", &XRFrame::GetHitTestResults),
                    InstanceMethod("createAnchor", &XRFrame::CreateAnchor),
                    InstanceMethod("getJointPose", &XRFrame::GetJointPose),
                    InstanceMethod("fillPoses", &XRFrame::FillPoses),
                    InstanceMethod("fillJointRadii", &XRFrame::FillJointRadii),
                    InstanceMethod("getImageTrackingResults", &XRFrame::GetImageTrackingResults),
                    InstanceAccessor("trackedAnchors", &XRFrame::GetTrackedAnchors, nullptr),
                    InstanceAccessor("worldInformation", &XRFrame::GetWorldInformation, nullptr),
                    InstanceAccessor("featurePointCloud", &XRFrame::GetFeaturePointCloud, nullptr),
                });

            env.Global().Set(JS_CLASS_NAME, func);
        }

        Napi::Object XRFrame::New(const Napi::CallbackInfo& info)
        {
            return info.Env().Global().Get(JS_CLASS_NAME).As<Napi::Function>().New({});
        }

        Napi::Object XRFrame::WrapForScript(const Napi::Object& frame)
        {
            const auto wrapper = JsRuntime::NativeObject::GetFromJavaScript(frame.Env()).Get("NativeXRFrame");
            return wrapper.IsFunction() ? wrapper.As<Napi::Function>().New({frame}) : frame;
        }

        XRFrame::XRFrame(const Napi::CallbackInfo& info)
            : Napi::ObjectWrap<XRFrame>{ info }
            , m_imageTrackingResultsArray{ Napi::Persistent(Napi::Array::New(info.Env())) }
            , m_jsTransform{ Napi::Persistent(XRRigidTransform::New(info.Env())) }
            , m_transform{ *XRRigidTransform::Unwrap(m_jsTransform.Value()) }
            , m_jsPose{ Napi::Persistent(Napi::Object::New(info.Env())) }
            , m_jsJointPose{ Napi::Persistent(Napi::Object::New(info.Env())) }
        {
            info.This().As<Napi::Object>().Set("_imageTrackingResults", m_imageTrackingResultsArray.Value());
            m_jsPose.Set("transform", m_jsTransform.Value());
            m_jsJointPose.Set("transform", m_jsTransform.Value());
        }

        void XRFrame::Update(const Napi::Env& env, const std::shared_ptr<const xr::System::Session::Frame>& frame, double timestamp)
        {
            // Store off a pointer to the frame so that the viewer pose can be updated later. We cannot
            // update the viewer pose here because we don't yet know the desired reference space.
            if (m_frame != frame)
            {
                m_jsXRViewerPose.Reset();
                m_viewerPoseSpace.Reset();
            }
            m_frame = frame;

            // Anchors created during the previous XR frame become observable from this frame.
            // Babylon's WebXRAnchorSystem registers a future anchor after createAnchor() resolves;
            // exposing the native anchor in the same frame can make the feature miss that future.
            PromotePendingAnchors();

            // Update anchor positions.
            UpdateAnchors();

            // Update scene objects.
            UpdateSceneObjects(env);

            // Update planes.
            UpdatePlanes(env, timestamp);

            // Update meshes.
            UpdateMeshes(env, timestamp);

            // Update image tracking results.
            UpdateImageTrackingResults(env);
        }

        Napi::Promise XRFrame::CreateNativeAnchor(const Napi::CallbackInfo& info, xr::Pose pose, xr::NativeTrackablePtr nativeTrackable)
        {
            // Create the native anchor.
            auto nativeAnchor = m_frame->CreateAnchor(pose, nativeTrackable);

            // Create the XRAnchor object, and initialize its members.
            auto napiAnchor = Napi::Persistent(XRAnchor::New(info.Env()));
            auto* xrAnchor = XRAnchor::Unwrap(napiAnchor.Value());
            xrAnchor->SetAnchor(nativeAnchor);

            // Expose the anchor to trackedAnchors on the next XR frame. Browser WebXR does
            // not make a hit-test-created anchor observable until a later frame, and Babylon's
            // anchor feature relies on that ordering to resolve its future anchor record.
            m_pendingAnchors.emplace_back(std::move(napiAnchor));

            // Resolve the promise with the newly created anchor.
            auto deferred = Napi::Promise::Deferred::New(info.Env());
            deferred.Resolve(m_pendingAnchors.back().Value());
            return deferred.Promise();
        }

        Napi::Value XRFrame::DeclareNativeAnchor(const Napi::Env& env, xr::NativeAnchorPtr nativeAnchor)
        {
            for (const auto& anchor : m_trackedAnchors)
            {
                const auto xrAnchor = XRAnchor::Unwrap(anchor.Value());
                if (xrAnchor->GetNativeAnchor().NativeAnchor == nativeAnchor)
                {
                    return anchor.Value();
                }
            }

            for (const auto& anchor : m_pendingAnchors)
            {
                const auto xrAnchor = XRAnchor::Unwrap(anchor.Value());
                if (xrAnchor->GetNativeAnchor().NativeAnchor == nativeAnchor)
                {
                    return anchor.Value();
                }
            }

            // Provide the native anchor to the native frame.
            auto newAnchor = m_frame->DeclareAnchor(nativeAnchor);

            // Create and populate the napi object.
            auto napiAnchor = Napi::Persistent(XRAnchor::New(env));
            auto xrAnchor = XRAnchor::Unwrap(napiAnchor.Value());
            xrAnchor->SetAnchor(newAnchor);

            // Track the napi object.
            m_trackedAnchors.emplace_back(std::move(napiAnchor));

            // Return the napi object.
            return napiAnchor.Value();
        }

        Napi::Value XRFrame::GetJSSceneObjectFromID(const Napi::CallbackInfo& info, const xr::System::Session::Frame::SceneObject::Identifier objectID)
        {
            if (objectID == xr::System::Session::Frame::SceneObject::INVALID_ID)
            {
                return info.Env().Undefined();
            }

            const auto sceneObjectIterator = m_sceneObjects.find(objectID);
            if (sceneObjectIterator == m_sceneObjects.end())
            {
                return info.Env().Undefined();
            }

            return sceneObjectIterator->second.Value();
        }

        Napi::Value XRFrame::GetViewerPose(const Napi::CallbackInfo& info)
        {
            if (!m_frame || (!m_hasBegunTracking && !m_frame->IsTracking))
            {
                return info.Env().Null();
            }
            m_hasBegunTracking = true;

            const auto space = info[0].As<Napi::Object>();
            if (!m_jsXRViewerPose.IsEmpty() && m_viewerPoseSpace.Value().StrictEquals(space))
            {
                return m_jsXRViewerPose.Value();
            }
            auto* referenceSpace = XRReferenceSpace::Unwrap(space);
            std::optional<xr::Pose> inverse;
            if (referenceSpace && (referenceSpace->HasOffset() || referenceSpace->IsViewer()))
                inverse = InvertPose(referenceSpace->OriginInNativeFrame(ViewerOrigin()));
            m_jsXRViewerPose = Napi::Persistent(XRViewerPose::New(info));
            m_viewerPoseSpace = Napi::Persistent(space);
            XRViewerPose::Unwrap(m_jsXRViewerPose.Value())->Update(
                info,
                m_frame,
                inverse);

            return m_jsXRViewerPose.Value();
        }

        Napi::Value XRFrame::GetPose(const Napi::CallbackInfo& info)
        {
            const auto env = info.Env();
            if (!m_poseAccess || !m_frame)
            {
                auto error = Napi::Error::New(env, "XRFrame.getPose requires an active XR frame.");
                error.Value().Set("name", "InvalidStateError");
                error.ThrowAsJavaScriptException();
                return env.Undefined();
            }
            const auto space = LocateSpace(info[0]);
            if (env.IsExceptionPending()) return env.Undefined();
            const auto base = LocateSpace(info[1]);
            if (env.IsExceptionPending()) return env.Undefined();
            if (!space || !base) return env.Null();
            auto result = XRPose::New(info);
            XRPose::Unwrap(result)->Update(info, ComposePoses(InvertPose(*base), *space));
            result.Set("emulatedPosition", !m_frame->IsTracking);
            return result;
        }

        xr::Pose XRFrame::ViewerOrigin() const
        {
            if (m_frame->ViewerSpace)
                return m_frame->ViewerSpace->Pose;
            return m_frame->Views.empty() ? xr::Pose{} : m_frame->Views.front().Space.Pose;
        }

        std::optional<xr::Pose> XRFrame::LocateSpace(Napi::Value value) const
        {
            const auto env = value.Env();
            if (value.IsExternal())
            {
                // Do not dereference an opaque space retained from a disconnected source.
                const auto* address = value.As<Napi::External<void>>().Data();
                for (const auto& source : m_frame->InputSources)
                {
                    if (!source.TrackedThisFrame || source.HasUnavailableControllerPose(address))
                    {
                        continue;
                    }
                    if (address == &source.AimSpace)
                        return source.AimSpace.Pose;
                    if (address == &source.GripSpace)
                        return source.GripSpace.Pose;
                    if (source.JointsTrackedThisFrame)
                    {
                        for (const auto& joint : source.HandJoints)
                        {
                            if (address == &joint && joint.PoseTracked)
                                return joint.Pose;
                        }
                    }
                }
                if (m_frame->EyeTrackerSpace && address == &*m_frame->EyeTrackerSpace)
                    return m_frame->EyeTrackerSpace->Pose;
                return std::nullopt;
            }
            if (value.IsObject() && value.As<Napi::Object>().InstanceOf(env.Global().Get("XRReferenceSpace").As<Napi::Function>()))
            {
                return XRReferenceSpace::Unwrap(value.As<Napi::Object>())->OriginInNativeFrame(ViewerOrigin());
            }
            Napi::TypeError::New(env, "Expected an XR space.").ThrowAsJavaScriptException();
            return std::nullopt;
        }

        Napi::Value XRFrame::GetPoseData(const Napi::CallbackInfo& info)
        {
            auto* referenceSpace = XRReferenceSpace::Unwrap(info[1].As<Napi::Object>());
            if (!info[0].IsExternal())
            {
                if (!m_poseAccess || !m_frame) return Napi::Boolean::New(info.Env(), false);
                const auto space = LocateSpace(info[0]);
                if (!space || !referenceSpace) return Napi::Boolean::New(info.Env(), false);
                const auto pose = ComposePoses(InvertPose(referenceSpace->OriginInNativeFrame(ViewerOrigin())), *space);
                auto vectors = info[2].As<Napi::ArrayBuffer>();
                auto matrix = info[3].As<Napi::ArrayBuffer>();
                m_transform.Update(xr::Space{pose}, vectors, matrix, false);
                return Napi::Boolean::New(info.Env(), true);
            }
            const auto* address = info[0].As<Napi::External<xr::Space>>().Data();
            if (!m_frame || std::any_of(m_frame->InputSources.begin(), m_frame->InputSources.end(),
                [address](const auto& source) { return source.HasUnavailableControllerPose(address); }))
            {
                return Napi::Boolean::New(info.Env(), false);
            }
            const auto& space = *address;
            auto vectorBuffer = info[2].As<Napi::ArrayBuffer>();
            auto matrixBuffer = info[3].As<Napi::ArrayBuffer>();
            m_transform.Update(xr::Space{referenceSpace->GetPoseInReferenceSpace(space.Pose)}, vectorBuffer, matrixBuffer, false);
            return Napi::Boolean::From(info.Env(), true);
        }

        Napi::Value XRFrame::GetJointPose(const Napi::CallbackInfo& info)
        {
            assert(info[0].IsExternal());

            const auto& jointSpace = *info[0].As<Napi::External<xr::System::Session::Frame::JointSpace>>().Data();

            if (jointSpace.PoseTracked)
            {
                auto* referenceSpace = XRReferenceSpace::Unwrap(info[1].As<Napi::Object>());
                m_transform.Update(xr::Space{referenceSpace->GetPoseInReferenceSpace(jointSpace.Pose)}, false);
                m_jsJointPose.Set("radius", jointSpace.PoseRadius);
                return m_jsJointPose.Value();
            }
            else
            {
                return info.Env().Undefined();
            }
        }

        Napi::Value XRFrame::FillPoses(const Napi::CallbackInfo& info)
        {
            const auto spaces = info[0].As<Napi::Array>();
            auto* referenceSpace = XRReferenceSpace::Unwrap(info[1].As<Napi::Object>());
            auto transforms = info[2].As<Napi::Float32Array>();
            if (spaces.Length() != (transforms.ElementLength() >> 4))
            {
                throw std::runtime_error{ "Number of spaces doesn't match number of transforms * 16." };
            }

            for (uint32_t spaceIdx = 0; spaceIdx < spaces.Length(); spaceIdx++)
            {
                const auto& jointSpace = *spaces[spaceIdx].As<Napi::External<xr::System::Session::Frame::JointSpace>>().Data();
                const auto transformMatrix = CreateTransformMatrix(xr::Space{referenceSpace->GetPoseInReferenceSpace(jointSpace.Pose)}, false);
                std::memcpy(transforms.Data() + (spaceIdx << 4), transformMatrix.data(), sizeof(float) << 4);
            }

            return Napi::Value::From(info.Env(), true);
        }

        Napi::Value XRFrame::FillJointRadii(const Napi::CallbackInfo& info)
        {
            const auto spaces = info[0].As<Napi::Array>();
            auto radii = info[1].As<Napi::Float32Array>();
            if (spaces.Length() != radii.ElementLength())
            {
                throw std::runtime_error{ "Number of spaces doesn't match number of radii." };
            }

            for (uint32_t spaceIdx = 0; spaceIdx < spaces.Length(); spaceIdx++)
            {
                const auto& jointSpace = *spaces[spaceIdx].As<Napi::External<xr::System::Session::Frame::JointSpace>>().Data();
                const auto jointRadius = jointSpace.PoseRadius;
                radii.Data()[spaceIdx] = jointRadius;
            }

            return Napi::Value::From(info.Env(), true);
        }

        Napi::Value XRFrame::GetHitTestResults(const Napi::CallbackInfo& info)
        {
            if (!m_frame || !m_poseAccess)
            {
                auto error = Napi::Error::New(info.Env(), "Hit testing requires an active XR frame.");
                error.Value().Set("name", "InvalidStateError");
                throw error;
            }
            XRHitTestSource* hitTestSource = XRHitTestSource::Unwrap(info[0].As<Napi::Object>());
            XRRay* offsetRay = hitTestSource->OffsetRay();
            xr::Ray nativeRay{};
            if (offsetRay != nullptr)
            {
                nativeRay = offsetRay->GetNativeRay();
            }
            else
            {
                nativeRay = { {0, 0, 0}, {0, 0, -1} };
            }

            const auto origin = LocateSpace(hitTestSource->Space());
            if (info.Env().IsExceptionPending()) return info.Env().Undefined();
            if (!origin || m_frame->Views.empty()) return Napi::Array::New(info.Env());
            // The ARKit and ARCore backends accept first-view-relative rays.
            const auto fromSource = ComposePoses(InvertPose(m_frame->Views.front().Space.Pose), *origin);
            nativeRay.Origin = ComposePoses(fromSource, xr::Pose{nativeRay.Origin, {}}).Position;
            nativeRay.Direction = RotatePoseVector(fromSource.Orientation, nativeRay.Direction);

            std::vector<xr::HitResult> nativeHitResults{};
            m_frame->GetHitTestResults(nativeHitResults, nativeRay, hitTestSource->GetEntityTypes());

            // Translate those results into a napi array.
            auto results = Napi::Array::New(info.Env(), nativeHitResults.size());
            uint32_t i{ 0 };
            for (std::vector<xr::HitResult>::iterator it = nativeHitResults.begin(); it != nativeHitResults.end(); ++it)
            {
                Napi::Object currentResult = XRHitTestResult::New(info);
                XRHitTestResult* xrResult = XRHitTestResult::Unwrap(currentResult);
                xrResult->SetHitResult(*it);
                xrResult->SetXRFrame(this);

                results[i++] = currentResult;
            }

            return results;
        }

        Napi::Value XRFrame::CreateAnchor(const Napi::CallbackInfo& info)
        {
            XRRigidTransform* transform = XRRigidTransform::Unwrap(info[0].As<Napi::Object>());
            auto* referenceSpace = XRReferenceSpace::Unwrap(info[1].As<Napi::Object>());
            return CreateNativeAnchor(info, referenceSpace->GetPoseInNativeSpace(transform->GetNativePose()), nullptr);
        }

        Napi::Value XRFrame::GetTrackedAnchors(const Napi::CallbackInfo& info)
        {
            // Create a JavaScript set to store all currently tracked anchors.
            Napi::Object anchorSet = info.Env().Global().Get("Set").As<Napi::Function>().New({});

            // Loop over the list of tracked anchors, and add them to the set.
            for (const Napi::ObjectReference& napiAnchorRef : m_trackedAnchors)
            {
                anchorSet.Get("add").As<Napi::Function>().Call(anchorSet, { napiAnchorRef.Value() });
            }

            return anchorSet;
        }

        void XRFrame::PromotePendingAnchors()
        {
            if (m_pendingAnchors.empty())
            {
                return;
            }

            for (auto& anchor : m_pendingAnchors)
            {
                m_trackedAnchors.emplace_back(std::move(anchor));
            }
            m_pendingAnchors.clear();
        }

        void XRFrame::UpdateAnchors()
        {
            // Loop over all anchors and update their state.
            std::vector<Napi::ObjectReference>::iterator anchorIter = m_trackedAnchors.begin();
            while (anchorIter != m_trackedAnchors.end())
            {
                XRAnchor* xrAnchor = XRAnchor::Unwrap((*anchorIter).Value());
                xr::Anchor& nativeAnchor = xrAnchor->GetNativeAnchor();

                // Update the anchor if it has not been marked for deletion.
                if (nativeAnchor.IsValid)
                {
                    m_frame->UpdateAnchor(nativeAnchor);
                }

                // If the anchor has been marked for deletion, delete the anchor from the session
                // and remove it from the list of tracked anchors.
                if (!nativeAnchor.IsValid)
                {
                    m_frame->DeleteAnchor(nativeAnchor);
                    anchorIter = m_trackedAnchors.erase(anchorIter);
                }
                else
                {
                    ++anchorIter;
                }
            }
        }

        Napi::Value XRFrame::GetWorldInformation(const Napi::CallbackInfo& info)
        {
            // Create a JavaScript object that stores all world information.
            Napi::Object worldInformationObj = Napi::Object::New(info.Env());

            // Create a set to contain all of the currently tracked planes.
            Napi::Object planeSet = info.Env().Global().Get("Set").As<Napi::Function>().New({});

            // Loop over the list of tracked planes, and add them to the set.
            for (const auto& [plane, planeNapiValue] : m_trackedPlanes)
            {
                planeSet.Get("add").As<Napi::Function>().Call(planeSet, { planeNapiValue.Value() });
            }

            // Pass the world information object back to the caller.
            worldInformationObj.Set("detectedPlanes", planeSet);

            if (m_frame)
            {
                if (m_meshSet.IsEmpty())
                {
                    auto meshes = info.Env().Global().Get("Set").As<Napi::Function>().New({});
                    for (const auto& entry : m_trackedMeshes)
                        meshes.Get("add").As<Napi::Function>().Call(meshes, {entry.second.Value()});
                    m_meshSet = Napi::Persistent(meshes);
                }
                worldInformationObj.Set("detectedMeshes", m_meshSet.Value());
            }

            return worldInformationObj;
        }

        Napi::Value XRFrame::GetFeaturePointCloud(const Napi::CallbackInfo& info)
        {
            // Get feature points from native.
            std::vector<xr::FeaturePoint>& pointCloud = m_frame->FeaturePointCloud;
            auto featurePointArray = Napi::Array::New(info.Env(), pointCloud.size() * 5);
            for (size_t i = 0; i < pointCloud.size(); i++)
            {
                int pointIndex = (int)i * 5;
                auto& featurePoint = pointCloud[i];
                featurePointArray.Set(pointIndex, Napi::Value::From(info.Env(), featurePoint.X));
                featurePointArray.Set(pointIndex + 1, Napi::Value::From(info.Env(), featurePoint.Y));
                featurePointArray.Set(pointIndex + 2, Napi::Value::From(info.Env(), featurePoint.Z));
                featurePointArray.Set(pointIndex + 3, Napi::Value::From(info.Env(), featurePoint.ConfidenceValue));
                featurePointArray.Set(pointIndex + 4, Napi::Value::From(info.Env(), featurePoint.ID));
            }

            return featurePointArray;
        }

        void XRFrame::UpdateSceneObjects(const Napi::Env& env)
        {
            for (const auto& sceneObjectID : m_frame->UpdatedSceneObjects)
            {
                if (m_sceneObjects.count(sceneObjectID) == 0)
                {
                    m_sceneObjects[sceneObjectID] = Napi::Persistent(Napi::Object::New(env));
                }

                const auto& sceneObject = m_frame->GetSceneObjectByID(sceneObjectID);
                const auto typeNameIterator = xr::SceneObjectTypeNames.find(sceneObject.Type);
                const auto& typeName = typeNameIterator != xr::SceneObjectTypeNames.end()
                    ? typeNameIterator->second
                    : xr::SceneObjectTypeNames.find(xr::SceneObjectType::Unknown)->second;
                m_sceneObjects.at(sceneObjectID).Value().Set("type", typeName);
            }

            for (const auto& removedObjectID : m_frame->RemovedSceneObjects)
            {
                m_sceneObjects.erase(removedObjectID);
            }
        }

        void XRFrame::UpdatePlanes(const Napi::Env& env, double timestamp)
        {
            // First loop over the list of updated planes, check if they exist in our map if not create them otherwise update them.
            for (auto planeID : m_frame->UpdatedPlanes)
            {
                XRPlane* xrPlane{};
                auto trackedPlaneIterator = m_trackedPlanes.find(planeID);

                // Plane does not yet exist create the JS object and insert it into the map.
                if (trackedPlaneIterator == m_trackedPlanes.end())
                {
                    auto napiPlane = Napi::Persistent(XRPlane::New(env));
                    xrPlane = XRPlane::Unwrap(napiPlane.Value());
                    xrPlane->SetNativePlaneId(planeID);
                    xrPlane->SetXRFrame(this);
                    m_trackedPlanes.insert({ planeID, std::move(napiPlane) });
                }
                else
                {
                    xrPlane = XRPlane::Unwrap(trackedPlaneIterator->second.Value());
                }

                xrPlane->Update(timestamp);
            }

            // Next go over removed planes and remove them from our mapping.
            for (auto planeID : m_frame->RemovedPlanes)
            {
                auto trackedPlaneIterator = m_trackedPlanes.find(planeID);
                assert(trackedPlaneIterator != m_trackedPlanes.end());
                m_trackedPlanes.erase(trackedPlaneIterator);
            }
        }

        void XRFrame::UpdateMeshes(const Napi::Env& env, double timestamp)
        {
            for (auto meshID : m_frame->UpdatedMeshes)
            {
                XRMesh* xrMesh{};
                auto trackedMeshIterator = m_trackedMeshes.find(meshID);

                if (trackedMeshIterator == m_trackedMeshes.end())
                {
                    auto napiMesh = Napi::Persistent(XRMesh::New(env));
                    xrMesh = XRMesh::Unwrap(napiMesh.Value());
                    xrMesh->SetNativeMeshId(meshID);
                    xrMesh->SetXRFrame(this);
                    m_trackedMeshes.insert({ meshID, std::move(napiMesh) });
                }
                else
                {
                    xrMesh = XRMesh::Unwrap(trackedMeshIterator->second.Value());
                }

                xrMesh->SetLastUpdatedTime(timestamp);
            }

            for (auto meshID : m_frame->RemovedMeshes)
            {
                m_trackedMeshes.erase(meshID);
            }

            // Materialize the new snapshot only if script requests worldInformation.
            m_meshSet.Reset();
        }

        void XRFrame::UpdateImageTrackingResults(const Napi::Env& env)
        {
            // Loop over the list of updated image tracking results, check if they exist in our map if not create them otherwise update them.
            for (auto imageTrackingResultID : m_frame->UpdatedImageTrackingResults)
            {
                const auto trackedImageTrackingResultIterator{ m_trackedImageIDToResultMap.find(imageTrackingResultID) };

                // Get the matching native result
                xr::System::Session::Frame::ImageTrackingResult& nativeResult{ m_frame->GetImageTrackingResultByID(imageTrackingResultID) };

                // Convert the image tracking state from the native result to a string.
                const std::string trackingStateString = nativeResult.TrackingState == xr::ImageTrackingState::TRACKED ? XRImageTrackingState::TRACKED : XRImageTrackingState::EMULATED;

                // Result does not yet exist, create the JS object and insert it into the map.
                if (trackedImageTrackingResultIterator == m_trackedImageIDToResultMap.end())
                {
                    // Don't add untracked images.
                    if (nativeResult.TrackingState == xr::ImageTrackingState::UNTRACKED)
                    {
                        continue;
                    }

                    auto napiResult{ Napi::Object::New(env) };
                    napiResult.Set("index", Napi::Value::From(env, nativeResult.Index));
                    napiResult.Set("trackingState", Napi::Value::From(env, trackingStateString));
                    napiResult.Set("measuredWidthInMeters", Napi::Value::From(env, nativeResult.MeasuredWidthInMeters));
                    napiResult.Set("imageSpace", Napi::External<xr::Space>::New(env, &nativeResult.ImageSpace));

                    auto napiResultRef{ Napi::Weak(napiResult) };
                    auto imageTrackingArray{ m_imageTrackingResultsArray.Value() };
                    imageTrackingArray.Set(imageTrackingArray.Length(), napiResultRef.Value());
                    m_trackedImageIDToResultMap.insert({ imageTrackingResultID, std::move(napiResultRef) });
                }
                else
                {
                    // Update the tracked image.
                    auto napiResult{ trackedImageTrackingResultIterator->second.Value().As<Napi::Object>() };
                    napiResult.Set("trackingState", Napi::Value::From(env, trackingStateString));
                    napiResult.Set("measuredWidthInMeters", Napi::Value::From(env, nativeResult.MeasuredWidthInMeters));
                }
            }
        }
    } // Plugins
} // Babylon
