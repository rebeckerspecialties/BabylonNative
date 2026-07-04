class DepthPrePassHookGuardPlugin extends BABYLON.MaterialPluginBase {
    constructor(material) {
        super(material, "DepthPrePassHookGuard", 500, {}, true, true);
    }

    isCompatible() {
        return true;
    }

    getCustomCode(shaderType, shaderLanguage) {
        if (shaderType !== "fragment") {
            return null;
        }

        if (shaderLanguage === BABYLON.ShaderLanguage.WGSL) {
            return {
                CUSTOM_FRAGMENT_BEFORE_LIGHTS: `
#ifdef DEPTHPREPASS
discard;
#endif
`,
                CUSTOM_FRAGMENT_MAIN_END: `
#ifdef DEPTHPREPASS
fragmentOutputs.color = vec4f(1.0, 0.0, 1.0, 1.0);
#endif
`,
            };
        }

        return {
            CUSTOM_FRAGMENT_BEFORE_LIGHTS: `
#ifdef DEPTHPREPASS
discard;
#endif
`,
            CUSTOM_FRAGMENT_MAIN_END: `
#ifdef DEPTHPREPASS
gl_FragColor = vec4(1.0, 0.0, 1.0, 1.0);
#endif
`,
        };
    }
}

function attachDepthPrePassGuard(material) {
    material.backFaceCulling = false;
    material.needDepthPrePass = true;
    material.transparencyMode = BABYLON.Material.MATERIAL_ALPHABLEND;
    new DepthPrePassHookGuardPlugin(material);
    return material;
}

function createStandardMaterial(scene, color) {
    const material = new BABYLON.StandardMaterial("standardDepthPrePass", scene);
    material.diffuseColor = color;
    material.specularColor = BABYLON.Color3.Black();
    material.alpha = 0.42;
    return attachDepthPrePassGuard(material);
}

function createPBRMaterial(scene, color) {
    const material = new BABYLON.PBRMaterial("pbrDepthPrePass", scene);
    material.albedoColor = color;
    material.metallic = 0;
    material.roughness = 0.68;
    material.alpha = 0.42;
    return attachDepthPrePassGuard(material);
}

function createOpenPBRMaterial(scene, color) {
    const material = new BABYLON.OpenPBRMaterial("openPBRDepthPrePass", scene);
    material.baseColor = color;
    material.baseDiffuseRoughness = 0.68;
    material.geometryOpacity = 0.42;
    return attachDepthPrePassGuard(material);
}

function addSphere(scene, name, x, material) {
    const sphere = BABYLON.MeshBuilder.CreateSphere(name, { segments: 64, diameter: 1.55 }, scene);
    sphere.position.x = x;
    sphere.material = material;
    return sphere;
}

function addBackdrop(scene, x, color) {
    const plane = BABYLON.MeshBuilder.CreatePlane("backdrop", { width: 1.9, height: 1.9 }, scene);
    plane.position.set(x, 0, 0.55);

    const material = new BABYLON.StandardMaterial("backdropMaterial", scene);
    material.diffuseColor = color;
    material.specularColor = BABYLON.Color3.Black();
    material.disableLighting = true;
    plane.material = material;
}

var createScene = function (engine) {
    const scene = new BABYLON.Scene(engine);
    scene.clearColor = new BABYLON.Color4(0.035, 0.04, 0.055, 1);
    scene.imageProcessingConfiguration.toneMappingEnabled = false;
    scene.imageProcessingConfiguration.exposure = 1;
    scene.imageProcessingConfiguration.contrast = 1;

    const camera = new BABYLON.FreeCamera("camera", new BABYLON.Vector3(0, 0, -7), scene);
    camera.mode = BABYLON.Camera.ORTHOGRAPHIC_CAMERA;
    camera.orthoLeft = -3.7;
    camera.orthoRight = 3.7;
    camera.orthoTop = 1.55;
    camera.orthoBottom = -1.55;
    camera.setTarget(BABYLON.Vector3.Zero());
    scene.activeCamera = camera;

    const light = new BABYLON.HemisphericLight("light", new BABYLON.Vector3(0.2, 1, -0.6), scene);
    light.intensity = 1.15;
    light.groundColor = new BABYLON.Color3(0.16, 0.18, 0.22);

    const slots = [
        { x: -2.35, color: new BABYLON.Color3(0.2, 0.54, 1.0), material: createStandardMaterial },
        { x: 0, color: new BABYLON.Color3(1.0, 0.34, 0.2), material: createPBRMaterial },
        { x: 2.35, color: new BABYLON.Color3(0.24, 0.92, 0.48), material: createOpenPBRMaterial },
    ];

    for (const slot of slots) {
        addBackdrop(scene, slot.x, new BABYLON.Color3(0.86, 0.88, 0.92));
        addSphere(scene, "depthPrePassSphere", slot.x, slot.material(scene, slot.color));
    }

    return scene;
};
