let roundtrip = false;
//options//

function installFetchPolyfillForValidation() {
    if (typeof globalThis.fetch === "function") {
        return;
    }

    globalThis.fetch = function (url) {
        const requestUrl = String(url);
        return BABYLON.Tools.LoadFileAsync(requestUrl, true).then((data) => {
            return {
                ok: true,
                status: 200,
                statusText: "OK",
                arrayBuffer: () => Promise.resolve(data),
                text: () => Promise.resolve(new TextDecoder().decode(new Uint8Array(data))),
            };
        });
    };
}

function createTexturedPlane(texture, position, suffix) {
    const plane = BABYLON.MeshBuilder.CreatePlane(`plane_${suffix}`);
    plane.position.fromArray(position);

    const material = new BABYLON.PBRMaterial(`material_${suffix}`);
    material.albedoTexture = texture;
    material.metallic = 0;
    material.roughness = 1;
    plane.material = material;
}

function withLoadTimeout(label, promise) {
    let timeout = null;
    return new Promise((resolve, reject) => {
        timeout = setTimeout(() => {
            reject(new Error(`Timed out loading ${label}`));
        }, 15000);

        promise.then((value) => {
            clearTimeout(timeout);
            resolve(value);
        }).catch((error) => {
            clearTimeout(timeout);
            reject(error);
        });
    });
}

var createScene = async function (engine) {
    installFetchPolyfillForValidation();

    let scene = new BABYLON.Scene(engine);
    scene.useDelayedTextureLoading = false;

    const textureSources = [
        "app:///Scripts/gltf-roundtrip-down.png",
        "app:///Scripts/gltf-roundtrip-grass.jpg",
        "app:///Scripts/gltf-roundtrip-sample_uastc.ktx2",
        "app:///Scripts/gltf-roundtrip-palm.png",
    ];

    const commonTextureOptions = {
        invertY: false,
        useSRGBBuffer: true,
    };

    const textureCreationMethods = [
        (url) => {
            return new Promise((resolve) => {
                let texture;
                texture = new BABYLON.Texture(url, scene, { onLoad: () => resolve(texture), ...commonTextureOptions });
            });
        },
        (url) => {
            const texture = new BABYLON.Texture(null, scene, commonTextureOptions);
            return new Promise((resolve) => {
                texture.updateURL(url, undefined, () => resolve(texture));
            });
        },
        async (url) => {
            const texture = new BABYLON.Texture(null, scene, { mimeType: BABYLON.GetMimeType(url), ...commonTextureOptions });
            const data = await BABYLON.Tools.LoadFileAsync(url);
            return new Promise((resolve) => {
                texture.updateURL(`data:foo/${url}`, data, () => resolve(texture));
            });
        },
        async (url) => {
            const texture = new BABYLON.Texture(null, scene, { mimeType: BABYLON.GetMimeType(url), ...commonTextureOptions });
            const data = await BABYLON.Tools.LoadFileAsync(url);
            return new Promise((resolve) => {
                texture.updateURL(`data:foo/${BABYLON.Tools.RandomId()}`, data, () => resolve(texture));
            });
        },
    ];

    const promises = [];

    textureSources.forEach((url, i) => {
        textureCreationMethods.forEach((method, j) => {
            promises.push(withLoadTimeout(`${url} via method ${j}`, (async () => {
                const texture = await method(url);
                createTexturedPlane(texture, [j, i, 0], `original${i}${j}`);
            })()));
        });
    });

    await Promise.all(promises);

    if (roundtrip) {
        const gltf = await BABYLON.GLTF2Export.GLBAsync(scene, "test");
        const blob = gltf.files["test.glb"];

        scene.rootNodes.forEach((node) => node.dispose(false, true));
        scene = await BABYLON.LoadSceneAsync(new File([blob], "test.glb"), engine);
    }

    scene.materials.forEach((material) => {
        material.debugMode = 20;
    });

    const target = new BABYLON.Vector3((textureSources.length / 2) - 0.5, (textureCreationMethods.length / 2) - 0.5, 0);
    const camera = new BABYLON.ArcRotateCamera("Camera", -Math.PI / 2, Math.PI / 2, textureSources.length * 1.2, target, scene);
    camera.attachControl(canvas, true);

    return scene;
};
