async function createScene(engine) {
    const scene = new BABYLON.Scene(engine);
    const camera = new BABYLON.ArcRotateCamera("Camera", 0, 0, 10, BABYLON.Vector3.Zero(), scene);
    const material = new BABYLON.StandardMaterial("kosh", scene);
    material.diffuseColor = BABYLON.Color3.Purple();
    new BABYLON.PointLight("Omni0", new BABYLON.Vector3(-17.6, 18.8, -49.9), scene);

    camera.setPosition(new BABYLON.Vector3(-15, 10, -20));
    camera.minZ = 1.0;
    camera.maxZ = 120.0;

    const skybox = BABYLON.Mesh.CreateBox("skyBox", 100.0, scene);
    const skyboxMaterial = new BABYLON.StandardMaterial("skyBox", scene);
    skyboxMaterial.backFaceCulling = false;
    skyboxMaterial.reflectionTexture = new BABYLON.CubeTexture("https://cdn.babylonjs.com/Assets/skybox/TropicalSunnyDay", scene);
    skyboxMaterial.reflectionTexture.coordinatesMode = BABYLON.Texture.SKYBOX_MODE;
    skyboxMaterial.diffuseColor = BABYLON.Color3.Black();
    skyboxMaterial.specularColor = BABYLON.Color3.Black();
    skyboxMaterial.disableLighting = true;
    skybox.material = skyboxMaterial;

    const shaderOptions = {
        attributes: ["position"],
        uniforms: ["worldViewProjection"]
    };
    if (engine.isWebGPU) {
        BABYLON.ShaderStore.ShadersStoreWGSL.customDepthVertexShader = `
            attribute position: vec3f;
            uniform worldViewProjection: mat4x4f;

            @vertex
            fn main(input: VertexInputs) -> FragmentInputs {
                vertexOutputs.position = uniforms.worldViewProjection * vec4f(vertexInputs.position, 1.0);
            }
        `;
        BABYLON.ShaderStore.ShadersStoreWGSL.customDepthPixelShader = `
            @fragment
            fn main(input: FragmentInputs) -> FragmentOutputs {
                let fragmentDepth = ${engine.useReverseDepthBuffer ? "1.0 - fragmentInputs.position.z" : "fragmentInputs.position.z"};
                let depth = 1.0 - (2.0 / (101.0 - fragmentDepth * 99.0));
                fragmentOutputs.color = vec4f(depth, depth, depth, 1.0);
            }
        `;
        shaderOptions.shaderLanguage = BABYLON.ShaderLanguage.WGSL;
    } else {
        BABYLON.Effect.ShadersStore.customDepthVertexShader = `
            attribute vec3 position;
            uniform mat4 worldViewProjection;

            void main(void) {
                gl_Position = worldViewProjection * vec4(position, 1.0);
            }
        `;
        BABYLON.Effect.ShadersStore.customDepthPixelShader = `
            void main(void) {
                float fragmentDepth = ${engine.useReverseDepthBuffer ? "1.0 - gl_FragCoord.z" : "gl_FragCoord.z"};
                float depth = 1.0 - (2.0 / (101.0 - fragmentDepth * 99.0));
                gl_FragColor = vec4(depth, depth, depth, 1.0);
            }
        `;
    }

    const depthMaterial = new BABYLON.ShaderMaterial("customDepth", scene, "customDepth", shaderOptions);
    depthMaterial.backFaceCulling = false;

    const plane = BABYLON.Mesh.CreatePlane("map", 10, scene);
    plane.billboardMode = BABYLON.AbstractMesh.BILLBOARDMODE_ALL;
    plane.scaling.y = 1.0 / engine.getAspectRatio(scene.activeCamera);

    const renderTarget = new BABYLON.RenderTargetTexture("depth", 1024, scene, true);
    renderTarget.renderList.push(skybox);
    scene.customRenderTargets.push(renderTarget);

    const spheresCount = 20;
    let alpha = 0;
    for (let index = 0; index < spheresCount; index++) {
        const sphere = BABYLON.Mesh.CreateSphere("Sphere" + index, 32, 3, scene);
        sphere.position.x = 10 * Math.cos(alpha);
        sphere.position.z = 10 * Math.sin(alpha);
        sphere.material = material;
        alpha += (2 * Math.PI) / spheresCount;
        renderTarget.renderList.push(sphere);
    }

    renderTarget.setMaterialForRendering(renderTarget.renderList, depthMaterial);

    const planeMaterial = new BABYLON.StandardMaterial("plan mat", scene);
    planeMaterial.emissiveTexture = renderTarget;
    planeMaterial.disableLighting = true;
    plane.material = planeMaterial;

    scene.registerBeforeRender(function () {
        camera.alpha += 0.01 * scene.getAnimationRatio();
    });

    return new Promise((resolve) => {
        const checkRenderTarget = function () {
            if (renderTarget.isReadyForRendering()) {
                resolve(scene);
            } else {
                window.setTimeout(checkRenderTarget, 10);
            }
        };
        checkRenderTarget();
    });
}
