// Validation-only prelude. Use the application's real session, layer, and adapter.
(() => {
    const Binding = globalThis.XRGPUBinding;
    globalThis.XRGPUBinding = class extends Binding {
        createProjectionLayer(init) {
            const preferred = this.getPreferredColorFormat();
            const explicit = super.createProjectionLayer(Object.freeze({ colorFormat: preferred }));
            const implicit = super.createProjectionLayer(Object.freeze({}));
            if (!(explicit instanceof XRProjectionLayer) || !(implicit instanceof XRProjectionLayer)) {
                throw new Error("XR projection layer creation failed for " + preferred);
            }
            let rejected = false;
            try {
                super.createProjectionLayer({ colorFormat: "not-a-gpu-format" });
            } catch {
                rejected = true;
            }
            if (!rejected) {
                throw new Error("XR projection layer accepted an invalid format");
            }
            console.log("XR projection format PASS before first frame: preferred=" + preferred);
            this.checkedEyes = new Set();
            return super.createProjectionLayer(init);
        }

        getViewSubImage(layer, view) {
            const image = super.getViewSubImage(layer, view);
            if (!this.checkedEyes.has(view.eye)) {
                if (image.colorTexture.format !== this.getPreferredColorFormat()) {
                    throw new Error("Unexpected " + view.eye + " eye texture format: " + image.colorTexture.format);
                }
                this.checkedEyes.add(view.eye);
                console.log("XR projection format PASS for " + view.eye + " eye: " + image.colorTexture.format);
            }
            return image;
        }
    };
})();
