(async function () {
    let currentScene;
    let config;
    const opts = (typeof _playgroundOptions === "object" && _playgroundOptions) ? _playgroundOptions : {};
    const justOnce = !!opts.runOnce;
    const saveResult = (typeof opts.saveResults === "boolean") ? opts.saveResults : true;
    const testWidth = 600;
    const testHeight = 400;
    const generateReferences = !!opts.generateReferences;
    const breakOnFail = !!opts.breakOnFail;
    const listTests = !!opts.listTests;
    const includeExcluded = !!opts.includeExcluded;
    const testFilters = Array.isArray(opts.testFilters) ? opts.testFilters.map(s => String(s).toLowerCase()) : [];
    const testIndices = Array.isArray(opts.testIndices) ? opts.testIndices.map(n => +n) : [];
    const sceneReadyTimeoutMs = (typeof opts.sceneReadyTimeoutMs === "number" && opts.sceneReadyTimeoutMs > 0)
        ? opts.sceneReadyTimeoutMs
        : 30000;
    // CLI --capture=N: 1-based frame index at which to call
    // TestUtils.captureNextFrame() for every executed test. The runner
    // extends each test's render budget so the .rdc finalizes.
    const cliCaptureFrame = (typeof opts.captureFrame === "number" && opts.captureFrame > 0) ? (opts.captureFrame | 0) : 0;
    // Frames after the trigger to let RenderDoc finalize the .rdc.
    const POST_CAPTURE_FRAMES = 5;

    function shouldRunTest(test, index) {
        if (testIndices.length > 0 && testIndices.indexOf(index) === -1) {
            return false;
        }
        if (testFilters.length > 0) {
            const title = (test.title || "").toLowerCase();
            for (let i = 0; i < testFilters.length; ++i) {
                if (title.indexOf(testFilters[i]) !== -1) {
                    return true;
                }
            }
            return false;
        }
        return true;
    }

    function failTest(done) {
        if (breakOnFail) {
            // Trigger the JS debugger if attached; on no-debugger runs the
            // host's bx exception filter prints a callstack on the next throw.
            // eslint-disable-next-line no-debugger
            debugger;
        }
        done(false);
    }

    function logNativeWebGPUStats(label) {
        if (typeof navigator === "undefined" || !navigator.gpu || typeof navigator.gpu._backendStats !== "function") {
            return;
        }

        try {
            const stats = navigator.gpu._backendStats();
            console.log(
                "NativeWebGPU stats " + label +
                ": pipelines=" + String(stats.renderPipelineCreateCount || 0) +
                " encoders=" + String(stats.commandEncoderCreateCount || 0) +
                " passes=" + String(stats.renderPassBeginCount || 0) +
                " submits=" + String(stats.queueSubmitCount || 0) +
                " draws=" + String(stats.drawCallCount || 0) +
                " textures=" + String(stats.textureCreateCount || 0) +
                " views=" + String(stats.textureViewCreateCount || 0) +
                " bindGroups=" + String(stats.bindGroupCreateCount || 0) +
                " buffers=" + String(stats.bufferCreateCount || 0) +
                " externalBorrowed=" + String(stats.externalImageUploadBorrowedCount || 0) +
                " externalOwned=" + String(stats.externalImageUploadOwnedCount || 0) +
                " drawPath=" + String(!!stats.drawPathActive) +
                " lastError=" + String(stats.lastError || "")
            );
        } catch (e) {
            // Stats are diagnostic-only.
        }
    }

    // Per-run counters surfaced as a final summary line on exit.
    let ranCount = 0;
    let passedCount = 0;
    let failedCount = 0;
    let skippedCount = 0;
    let missingRefCount = 0;
    let lastConsoleError = "";
    let validationWebGPUDevice = null;

    const originalConsoleError = console.error;
    console.error = function () {
        lastConsoleError = Array.prototype.map.call(arguments, formatLogArgument).join(" ");
        originalConsoleError.apply(console, arguments);
    };

    function formatLogArgument(value) {
        if (value === undefined) {
            return "undefined";
        }
        if (value === null) {
            return "null";
        }
        if (value && value.stack) {
            return value.stack;
        }
        try {
            return String(value);
        } catch (e) {
            return Object.prototype.toString.call(value);
        }
    }

    function getExclusionReason(t) {
        const isNativeWebGPU = !!globalThis.__babylonNativeValidationUseWebGPU;
        const includeInNativeWebGPU = isNativeWebGPU && !!t.includeInNativeWebGPU;
        if (t.excludeFromNativeWebGPU && isNativeWebGPU) {
            return "excludeFromNativeWebGPU" + (t.reason ? ": " + t.reason : "");
        }
        if (t.onlyVisual && !includeInNativeWebGPU) {
            return "onlyVisual";
        }
        if (t.excludeFromAutomaticTesting && !includeInNativeWebGPU) {
            return "excludeFromAutomaticTesting" + (t.reason ? ": " + t.reason : "");
        }
        if (t.excludedGraphicsApis && t.excludedGraphicsApis.includes(TestUtils.getGraphicsApiName())) {
            return "excludedGraphicsApis: " + TestUtils.getGraphicsApiName();
        }
        return null;
    }

    function getSkipReason(t) {
        if (includeExcluded) {
            return null;
        }
        return getExclusionReason(t);
    }

    function logRunSummary() {
        console.log("Run complete. ran=" + ranCount +
                    " passed=" + passedCount +
                    " failed=" + failedCount +
                    " missingRef=" + missingRefCount +
                    " skipped=" + skippedCount);
    }

    function createClassList() {
        const values = [];
        return {
            add: function () {
                for (let i = 0; i < arguments.length; ++i) {
                    const value = String(arguments[i]);
                    if (values.indexOf(value) === -1) {
                        values.push(value);
                    }
                }
            },
            remove: function () {
                for (let i = 0; i < arguments.length; ++i) {
                    const value = String(arguments[i]);
                    const index = values.indexOf(value);
                    if (index !== -1) {
                        values.splice(index, 1);
                    }
                }
            },
            contains: function (value) {
                return values.indexOf(String(value)) !== -1;
            },
            toggle: function (value, force) {
                const exists = this.contains(value);
                if (force === true || (!exists && force !== false)) {
                    this.add(value);
                    return true;
                }
                if (exists) {
                    this.remove(value);
                }
                return false;
            },
            toString: function () {
                return values.join(" ");
            }
        };
    }

    function augmentValidationElement(element, type, width, height) {
        const tagName = String(type || "div").toUpperCase();
        element.nodeType = element.nodeType || 1;
        element.nodeName = element.nodeName || tagName;
        element.tagName = element.tagName || tagName;
        element.style = element.style || {};
        element.attributes = element.attributes || {};
        element.children = element.children || [];
        element.childNodes = element.childNodes || element.children;
        element.classList = element.classList || createClassList();
        element.ownerDocument = element.ownerDocument || (typeof document !== "undefined" ? document : null);
        element.clientWidth = element.clientWidth || width || testWidth;
        element.clientHeight = element.clientHeight || height || testHeight;
        element.width = element.width || width || element.clientWidth;
        element.height = element.height || height || element.clientHeight;
        element.parentNode = element.parentNode || null;
        element.appendChild = element.appendChild || function (child) {
            if (child) {
                child.parentNode = element;
                element.children.push(child);
            }
            return child;
        };
        element.removeChild = element.removeChild || function (child) {
            const index = element.children.indexOf(child);
            if (index !== -1) {
                element.children.splice(index, 1);
                child.parentNode = null;
            }
            return child;
        };
        element.insertBefore = element.insertBefore || function (child, reference) {
            if (!child) {
                return child;
            }
            const index = element.children.indexOf(reference);
            child.parentNode = element;
            if (index === -1) {
                element.children.push(child);
            } else {
                element.children.splice(index, 0, child);
            }
            return child;
        };
        element.remove = element.remove || function () {
            if (element.parentNode && element.parentNode.removeChild) {
                element.parentNode.removeChild(element);
            }
        };
        element.cloneNode = element.cloneNode || function (deep) {
            const clone = createValidationElement(tagName.toLowerCase());
            clone.id = element.id || "";
            clone.className = element.className || "";
            clone.textContent = element.textContent || "";
            clone.innerHTML = element.innerHTML || "";
            for (const styleName in element.style) {
                clone.style[styleName] = element.style[styleName];
            }
            for (const attributeName in element.attributes) {
                clone.setAttribute(attributeName, element.attributes[attributeName]);
            }
            if (element.src && "src" in clone) {
                clone.src = element.src;
            }
            if (deep) {
                for (let i = 0; i < element.children.length; ++i) {
                    const child = element.children[i];
                    clone.appendChild(child && typeof child.cloneNode === "function" ? child.cloneNode(true) : child);
                }
            }
            return clone;
        };
        element.setAttribute = element.setAttribute || function (name, value) {
            element.attributes[String(name)] = String(value);
            element[String(name)] = value;
        };
        element.getAttribute = element.getAttribute || function (name) {
            const key = String(name);
            return Object.prototype.hasOwnProperty.call(element.attributes, key) ? element.attributes[key] : null;
        };
        element.removeAttribute = element.removeAttribute || function (name) {
            delete element.attributes[String(name)];
        };
        if (!element.__validationEventTarget) {
            const eventListeners = {};
            element.addEventListener = function (type, listener) {
                if (typeof listener !== "function") {
                    return;
                }
                const eventType = String(type);
                const listeners = eventListeners[eventType] || (eventListeners[eventType] = []);
                if (listeners.indexOf(listener) === -1) {
                    listeners.push(listener);
                }
            };
            element.removeEventListener = function (type, listener) {
                const listeners = eventListeners[String(type)];
                if (!listeners) {
                    return;
                }
                const index = listeners.indexOf(listener);
                if (index !== -1) {
                    listeners.splice(index, 1);
                }
            };
            element.dispatchEvent = function (event) {
                const eventObject = typeof event === "string" ? { type: event } : (event || {});
                const eventType = String(eventObject.type || "");
                try {
                    eventObject.target = eventObject.target || element;
                    eventObject.currentTarget = element;
                } catch (e) {
                    // Some event-like objects expose read-only target fields.
                }

                const propertyHandler = element["on" + eventType];
                if (typeof propertyHandler === "function") {
                    propertyHandler.call(element, eventObject);
                }

                const listeners = eventListeners[eventType];
                if (listeners) {
                    const snapshot = listeners.slice();
                    for (let i = 0; i < snapshot.length; ++i) {
                        snapshot[i].call(element, eventObject);
                    }
                }
                return !eventObject.defaultPrevented;
            };
            element.__validationEventTarget = true;
        }
        element.focus = element.focus || function () { };
        element.blur = element.blur || function () { };
        element.getBoundingClientRect = element.getBoundingClientRect || function () {
            return {
                x: 0,
                y: 0,
                top: 0,
                left: 0,
                right: element.clientWidth || element.width || testWidth,
                bottom: element.clientHeight || element.height || testHeight,
                width: element.clientWidth || element.width || testWidth,
                height: element.clientHeight || element.height || testHeight
            };
        };
        return element;
    }

    function findPropertyDescriptor(object, name) {
        let current = object;
        while (current) {
            const descriptor = Object.getOwnPropertyDescriptor(current, name);
            if (descriptor) {
                return descriptor;
            }
            current = Object.getPrototypeOf(current);
        }
        return null;
    }

    function createFallbackContext2D() {
        return {
            canvas: null,
            clearRect: function () { },
            fillRect: function () { },
            strokeRect: function () { },
            beginPath: function () { },
            closePath: function () { },
            moveTo: function () { },
            lineTo: function () { },
            arc: function () { },
            fill: function () { },
            stroke: function () { },
            save: function () { },
            restore: function () { },
            translate: function () { },
            rotate: function () { },
            scale: function () { },
            drawImage: function () { },
            putImageData: function () { },
            getImageData: function (x, y, width, height) {
                return {
                    width: width,
                    height: height,
                    data: new Uint8ClampedArray(Math.max(0, width * height * 4))
                };
            },
            createLinearGradient: function () {
                return { addColorStop: function () { } };
            },
            createRadialGradient: function () {
                return { addColorStop: function () { } };
            },
            measureText: function (text) {
                return { width: String(text).length * 8 };
            },
            fillText: function () { }
        };
    }

    function createOffscreenCanvas(width, height) {
        if (typeof _native !== "undefined" && _native.Canvas) {
            const nativeCanvas = new _native.Canvas();
            nativeCanvas.width = width;
            nativeCanvas.height = height;
            return augmentValidationElement(nativeCanvas, "canvas", width, height);
        }

        const canvasElement = augmentValidationElement({
            getContext: function () {
                const context = createFallbackContext2D();
                context.canvas = canvasElement;
                return context;
            }
        }, "canvas", width, height);
        return canvasElement;
    }

    function createValidationElement(type) {
        const tag = String(type).toLowerCase();
        if (tag === "canvas") {
            return createOffscreenCanvas(64, 64);
        }
        if (tag === "img") {
            return createValidationImageElement(0, 0);
        }
        return augmentValidationElement({}, type, 0, 0);
    }

    function canvasFromNativeImage(image) {
        const width = image.naturalWidth || image.width || 1;
        const height = image.naturalHeight || image.height || 1;
        const bitmapCanvas = createOffscreenCanvas(width, height);
        const context = bitmapCanvas.getContext("2d");
        if (typeof context.flush === "function") {
            context.flush();
        }
        context.drawImage(image, 0, 0);
        if (typeof context.flush === "function") {
            context.flush();
        }
        bitmapCanvas.close = bitmapCanvas.close || function () {
            if (typeof bitmapCanvas.dispose === "function") {
                bitmapCanvas.dispose();
            }
        };
        return bitmapCanvas;
    }

    function createValidationImageElement(width, height) {
        if (typeof _native === "undefined" || !_native.Image) {
            return augmentValidationElement({}, "img", width || 0, height || 0);
        }

        const image = augmentValidationElement(new _native.Image(), "img", width || 0, height || 0);
        const srcDescriptor = findPropertyDescriptor(image, "src");
        const onloadDescriptor = findPropertyDescriptor(image, "onload");
        const onerrorDescriptor = findPropertyDescriptor(image, "onerror");
        let srcValue = "";

        function setNativeSrc(value) {
            if (srcDescriptor && typeof srcDescriptor.set === "function") {
                srcDescriptor.set.call(image, value);
            }
        }

        function dispatchImageEvent(type, error) {
            if (type === "load") {
                image.complete = true;
            }
            const event = { type: type, target: image, currentTarget: image };
            if (error) {
                event.error = error;
                event.message = error.message || String(error);
            }
            image.dispatchEvent(event);
        }

        if (onloadDescriptor && typeof onloadDescriptor.set === "function") {
            onloadDescriptor.set.call(image, function () {
                dispatchImageEvent("load");
            });
        }
        if (onerrorDescriptor && typeof onerrorDescriptor.set === "function") {
            onerrorDescriptor.set.call(image, function (error) {
                dispatchImageEvent("error", error);
            });
        }

        Object.defineProperty(image, "onload", {
            configurable: true,
            enumerable: true,
            get: function () {
                return image.__validationOnload || null;
            },
            set: function (handler) {
                image.__validationOnload = typeof handler === "function" ? handler : null;
            }
        });
        Object.defineProperty(image, "onerror", {
            configurable: true,
            enumerable: true,
            get: function () {
                return image.__validationOnerror || null;
            },
            set: function (handler) {
                image.__validationOnerror = typeof handler === "function" ? handler : null;
            }
        });
        Object.defineProperty(image, "src", {
            configurable: true,
            enumerable: true,
            get: function () {
                return srcValue;
            },
            set: function (value) {
                srcValue = String(value || "");
                image.complete = false;

                if (isValidationObjectUrl(srcValue)) {
                    const blob = validationObjectUrls[srcValue];
                    blobToArrayBuffer(blob).then(function (buffer) {
                        const bytes = toUint8Array(buffer);
                        const dataUrl = "data:" + (blob.type || "application/octet-stream") + ";base64," + bytesToBase64(bytes);
                        setNativeSrc(dataUrl);
                    }).catch(function (error) {
                        dispatchImageEvent("error", error);
                    });
                    return;
                }

                setNativeSrc(srcValue);
            }
        });
        image.decode = image.decode || function () {
            if (image.complete) {
                return Promise.resolve();
            }
            return new Promise(function (resolve, reject) {
                image.addEventListener("load", function onLoad() {
                    image.removeEventListener("load", onLoad);
                    resolve();
                });
                image.addEventListener("error", function onError(event) {
                    image.removeEventListener("error", onError);
                    reject(event && event.error ? event.error : new Error("Native image decode failed."));
                });
            });
        };
        image.close = image.close || function () {
            if (typeof image.dispose === "function") {
                image.dispose();
            }
        };
        return image;
    }

    function installValidationWebGPUDeviceCapture() {
        if (typeof navigator === "undefined" || !navigator.gpu || navigator.gpu.__nativeValidationDeviceCaptureInstalled) {
            return;
        }

        const originalRequestAdapter = navigator.gpu.requestAdapter;
        if (typeof originalRequestAdapter !== "function") {
            return;
        }

        navigator.gpu.requestAdapter = function () {
            return Promise.resolve(originalRequestAdapter.apply(this, arguments)).then(function (adapter) {
                if (!adapter || adapter.__nativeValidationDeviceCaptureInstalled) {
                    return adapter;
                }

                const originalRequestDevice = adapter.requestDevice;
                if (typeof originalRequestDevice !== "function") {
                    return adapter;
                }

                adapter.requestDevice = function () {
                    return Promise.resolve(originalRequestDevice.apply(this, arguments)).then(function (device) {
                        validationWebGPUDevice = device || validationWebGPUDevice;
                        return device;
                    });
                };
                adapter.__nativeValidationDeviceCaptureInstalled = true;
                return adapter;
            });
        };
        navigator.gpu.__nativeValidationDeviceCaptureInstalled = true;
    }

    function waitForValidationWebGPUQueue() {
        if (!validationWebGPUDevice ||
            !validationWebGPUDevice.queue ||
            typeof validationWebGPUDevice.queue.onSubmittedWorkDone !== "function") {
            return Promise.resolve();
        }

        return validationWebGPUDevice.queue.onSubmittedWorkDone();
    }

    function installValidationBrowserShims() {
        installValidationBlobShim();
        installValidationWebGPUDeviceCapture();

        globalThis.window = globalThis.window || globalThis;
        globalThis.window.scrollX = globalThis.window.scrollX || 0;
        globalThis.window.scrollY = globalThis.window.scrollY || 0;
        globalThis.window.getComputedStyle = globalThis.window.getComputedStyle || function (element) {
            return element && element.style ? element.style : {};
        };
        globalThis.window.addEventListener = globalThis.window.addEventListener || function () { };
        globalThis.window.removeEventListener = globalThis.window.removeEventListener || function () { };

        const validationDocument = {
            createElement: function (type) {
                const element = createValidationElement(type);
                element.ownerDocument = validationDocument;
                return element;
            },
            createElementNS: function (namespace, type) {
                const element = createValidationElement(type);
                element.namespaceURI = namespace;
                element.ownerDocument = validationDocument;
                return element;
            },
            createTextNode: function (text) {
                return {
                    nodeType: 3,
                    nodeName: "#text",
                    textContent: String(text),
                    parentNode: null
                };
            },
            getElementsByTagName: function (tagName) {
                switch (String(tagName).toLowerCase()) {
                    case "head":
                        return [validationDocument.head];
                    case "body":
                        return [validationDocument.body];
                    case "html":
                        return [validationDocument.documentElement];
                    default:
                        return [];
                }
            },
            getElementById: function () {
                return null;
            },
            querySelector: function () {
                return null;
            },
            querySelectorAll: function () {
                return [];
            },
            addEventListener: function () { },
            removeEventListener: function () { },
            dispatchEvent: function () { return true; }
        };
        validationDocument.body = augmentValidationElement({}, "body", testWidth, testHeight);
        validationDocument.head = augmentValidationElement({}, "head", testWidth, 0);
        validationDocument.documentElement = augmentValidationElement({}, "html", testWidth, testHeight);
        validationDocument.body.ownerDocument = validationDocument;
        validationDocument.head.ownerDocument = validationDocument;
        validationDocument.documentElement.ownerDocument = validationDocument;

        globalThis.document = validationDocument;
        globalThis.OffscreenCanvas = createOffscreenCanvas;
        if (typeof globalThis.HTMLImageElement === "undefined") {
            globalThis.HTMLImageElement = function HTMLImageElement() { };
        }
        if (typeof globalThis.HTMLCanvasElement === "undefined") {
            globalThis.HTMLCanvasElement = function HTMLCanvasElement() { };
        }
        if (typeof _native !== "undefined" && _native.Image) {
            globalThis.Image = function Image(width, height) {
                return createValidationImageElement(width, height);
            };
            globalThis.Image.prototype = _native.Image.prototype;
        }
        if (typeof globalThis.Path2D === "undefined" && typeof _native !== "undefined" && _native.Path2D) {
            globalThis.Path2D = _native.Path2D;
        }
    }

    const validationObjectUrls = {};
    let nextValidationObjectUrlId = 1;

    function isValidationObjectUrl(url) {
        return typeof url === "string" && Object.prototype.hasOwnProperty.call(validationObjectUrls, url);
    }

    function toUint8Array(input) {
        if (input instanceof ArrayBuffer) {
            return new Uint8Array(input);
        }
        if (ArrayBuffer.isView(input)) {
            return new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
        }
        return input;
    }

    function encodeUtf8(text) {
        const value = String(text);
        if (typeof TextEncoder !== "undefined") {
            return new TextEncoder().encode(value);
        }

        const bytes = [];
        for (let i = 0; i < value.length; ++i) {
            let codePoint = value.charCodeAt(i);
            if (codePoint >= 0xd800 && codePoint <= 0xdbff && i + 1 < value.length) {
                const low = value.charCodeAt(++i);
                codePoint = 0x10000 + ((codePoint - 0xd800) << 10) + (low - 0xdc00);
            }

            if (codePoint < 0x80) {
                bytes.push(codePoint);
            } else if (codePoint < 0x800) {
                bytes.push(0xc0 | (codePoint >> 6), 0x80 | (codePoint & 0x3f));
            } else if (codePoint < 0x10000) {
                bytes.push(0xe0 | (codePoint >> 12), 0x80 | ((codePoint >> 6) & 0x3f), 0x80 | (codePoint & 0x3f));
            } else {
                bytes.push(0xf0 | (codePoint >> 18), 0x80 | ((codePoint >> 12) & 0x3f), 0x80 | ((codePoint >> 6) & 0x3f), 0x80 | (codePoint & 0x3f));
            }
        }
        return new Uint8Array(bytes);
    }

    function decodeUtf8(bytes) {
        const data = toUint8Array(bytes);
        if (typeof TextDecoder !== "undefined") {
            return new TextDecoder().decode(data);
        }

        let output = "";
        for (let i = 0; i < data.length; ++i) {
            output += String.fromCharCode(data[i]);
        }
        return output;
    }

    function bytesToBase64(bytes) {
        const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        let output = "";
        for (let i = 0; i < bytes.length; i += 3) {
            const a = bytes[i];
            const b = i + 1 < bytes.length ? bytes[i + 1] : 0;
            const c = i + 2 < bytes.length ? bytes[i + 2] : 0;
            output += alphabet[a >> 2];
            output += alphabet[((a & 3) << 4) | (b >> 4)];
            output += i + 1 < bytes.length ? alphabet[((b & 15) << 2) | (c >> 6)] : "=";
            output += i + 2 < bytes.length ? alphabet[c & 63] : "=";
        }
        return output;
    }

    function blobToArrayBuffer(blob) {
        if (!blob) {
            return Promise.reject(new Error("Expected Blob-like image source; got " + String(blob) + "."));
        }
        if (blob instanceof ArrayBuffer) {
            return Promise.resolve(blob);
        }
        if (ArrayBuffer.isView(blob)) {
            return Promise.resolve(blob.buffer.slice(blob.byteOffset, blob.byteOffset + blob.byteLength));
        }
        if (typeof blob.arrayBuffer === "function") {
            return blob.arrayBuffer();
        }
        if (typeof blob.bytes === "function") {
            return blob.bytes().then(function (bytes) {
                const data = toUint8Array(bytes);
                return data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
            });
        }
        return Promise.reject(new Error(
            "Expected Blob-like image source; got type=" + (typeof blob) +
            " ctor=" + (blob.constructor && blob.constructor.name ? blob.constructor.name : "(none)") +
            " keys=[" + Object.keys(blob).join(",") + "]."));
    }

    function blobPartToArrayBuffer(part) {
        if (part && typeof part.then === "function") {
            return part.then(blobPartToArrayBuffer);
        }
        if (part instanceof ArrayBuffer) {
            return Promise.resolve(part);
        }
        if (ArrayBuffer.isView(part)) {
            return Promise.resolve(part.buffer.slice(part.byteOffset, part.byteOffset + part.byteLength));
        }
        if (typeof part === "string") {
            const bytes = encodeUtf8(part);
            return Promise.resolve(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength));
        }
        if (part && typeof part.arrayBuffer === "function") {
            return part.arrayBuffer();
        }
        if (part && typeof part.bytes === "function") {
            return part.bytes().then(function (bytes) {
                const data = toUint8Array(bytes);
                return data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
            });
        }
        return Promise.reject(new Error("Unsupported BlobPart for validation."));
    }

    function createValidationBlobConstructor() {
        function ValidationBlob(parts, options) {
            const blobParts = Array.isArray(parts) ? parts.slice() : [];
            const type = options && options.type ? String(options.type) : "";
            Object.defineProperty(this, "_validationBlobParts", {
                configurable: false,
                enumerable: false,
                value: blobParts
            });
            Object.defineProperty(this, "type", {
                configurable: false,
                enumerable: true,
                value: type
            });

            let size = 0;
            for (let i = 0; i < blobParts.length; ++i) {
                const part = blobParts[i];
                if (part instanceof ArrayBuffer) {
                    size += part.byteLength;
                } else if (ArrayBuffer.isView(part)) {
                    size += part.byteLength;
                } else if (typeof part === "string") {
                    size += encodeUtf8(part).byteLength;
                } else if (part && typeof part.size === "number") {
                    size += part.size;
                }
            }
            Object.defineProperty(this, "size", {
                configurable: false,
                enumerable: true,
                value: size
            });
        }

        ValidationBlob.prototype.arrayBuffer = function () {
            return Promise.all(this._validationBlobParts.map(blobPartToArrayBuffer)).then(function (buffers) {
                let byteLength = 0;
                for (let i = 0; i < buffers.length; ++i) {
                    byteLength += buffers[i].byteLength;
                }

                const output = new Uint8Array(byteLength);
                let offset = 0;
                for (let i = 0; i < buffers.length; ++i) {
                    const bytes = new Uint8Array(buffers[i]);
                    output.set(bytes, offset);
                    offset += bytes.byteLength;
                }
                return output.buffer;
            });
        };
        ValidationBlob.prototype.bytes = function () {
            return this.arrayBuffer().then(function (buffer) {
                return new Uint8Array(buffer);
            });
        };
        ValidationBlob.prototype.text = function () {
            return this.arrayBuffer().then(function (buffer) {
                return decodeUtf8(new Uint8Array(buffer));
            });
        };
        ValidationBlob.prototype.slice = function (start, end, contentType) {
            const begin = Math.max(0, start || 0);
            return new ValidationBlob([this.arrayBuffer().then(function (buffer) {
                return buffer.slice(begin, end === undefined ? buffer.byteLength : end);
            })], { type: contentType || this.type });
        };
        return ValidationBlob;
    }

    function installValidationBlobShim() {
        let needsBlobShim = typeof globalThis.Blob === "undefined";
        if (!needsBlobShim) {
            try {
                needsBlobShim = typeof (new globalThis.Blob([])).arrayBuffer !== "function";
            } catch (e) {
                needsBlobShim = true;
            }
        }

        if (needsBlobShim) {
            globalThis.Blob = createValidationBlobConstructor();
        }
    }

    function imageFromDataUrl(dataUrl) {
        return new Promise(function (resolve, reject) {
            if (typeof _native === "undefined" || !_native.Image || !_native.Canvas) {
                reject(new Error("Native Canvas image support is unavailable."));
                return;
            }

            const image = new _native.Image();
            image.onload = function () {
                try {
                    image.close = image.close || function () {
                        if (typeof image.dispose === "function") {
                            image.dispose();
                        }
                    };
                    resolve(image);
                } catch (error) {
                    reject(error);
                }
            };
            image.onerror = function (error) {
                reject(error instanceof Error ? error : new Error("Native image decode failed."));
            };
            image.src = dataUrl;
        });
    }

    function imageFromArrayBuffer(buffer, mimeType) {
        const bytes = toUint8Array(buffer);
        const dataUrl = "data:" + (mimeType || "application/octet-stream") + ";base64," + bytesToBase64(bytes);
        return imageFromDataUrl(dataUrl);
    }

    function installValidationImageLoadingShim() {
        globalThis.URL = globalThis.URL || {};
        URL.createObjectURL = function (blob) {
            const url = "blob:native-validation/" + nextValidationObjectUrlId++;
            validationObjectUrls[url] = blob;
            return url;
        };
        URL.revokeObjectURL = function (url) {
            delete validationObjectUrls[url];
        };

        const originalLoadFile = BABYLON.Tools.LoadFile;
        const originalFileToolsLoadFile = BABYLON.FileTools && BABYLON.FileTools.LoadFile;

        function loadFileWithValidationBlobUrls(url, onSuccess, onProgress, offlineProvider, useArrayBuffer, onError, onOpened) {
            if (isValidationObjectUrl(url)) {
                const blob = validationObjectUrls[url];
                const promise = useArrayBuffer && blob.arrayBuffer ? blob.arrayBuffer() : blob.text();
                promise.then(function (data) {
                    onSuccess(data, url);
                }).catch(function (error) {
                    if (onError) {
                        onError(undefined, error);
                    } else {
                        throw error;
                    }
                });
                return {
                    abort: function () { },
                    onCompleteObservable: { add: function () { } }
                };
            }

            return originalLoadFile(url, onSuccess, onProgress, offlineProvider, useArrayBuffer, onError, onOpened);
        }

        BABYLON.Tools.LoadFile = loadFileWithValidationBlobUrls;
        if (BABYLON.FileTools && originalFileToolsLoadFile) {
            BABYLON.FileTools.LoadFile = loadFileWithValidationBlobUrls;
        }
        if (BABYLON.Engine) {
            BABYLON.Engine._FileToolsLoadFile = loadFileWithValidationBlobUrls;
        }
        if (BABYLON.ThinEngine) {
            BABYLON.ThinEngine._FileToolsLoadFile = loadFileWithValidationBlobUrls;
        }
        if (BABYLON.AbstractEngine) {
            BABYLON.AbstractEngine._FileToolsLoadFile = loadFileWithValidationBlobUrls;
        }

        globalThis.createImageBitmap = function (source, options) {
            if (typeof Blob !== "undefined" && source instanceof Blob) {
                return blobToArrayBuffer(source).then(function (buffer) {
                    return imageFromArrayBuffer(buffer, source.type);
                });
            }
            if (source instanceof ArrayBuffer || ArrayBuffer.isView(source)) {
                return imageFromArrayBuffer(source);
            }
            if (source && typeof source._getNativeImageData === "function") {
                return Promise.resolve(source);
            }
            if (source && typeof source.getCanvasTexture === "function") {
                return Promise.resolve(source);
            }
            if (source && typeof source.src === "string") {
                return imageFromDataUrl(source.src);
            }
            return Promise.reject(new Error("Unsupported createImageBitmap source for validation."));
        };

        function loadImageWithNativeCanvas(source, onLoad, onError, offlineProvider, mimeType, imageBitmapOptions, engine) {
            function reportError(message, error) {
                if (onError) {
                    onError(message, error);
                }
            }

            function decodeBuffer(buffer, type) {
                imageFromArrayBuffer(buffer, type || mimeType).then(onLoad).catch(function (error) {
                    reportError("Error while trying to load image.", error);
                });
            }

            if (typeof Blob !== "undefined" && source instanceof Blob) {
                blobToArrayBuffer(source).then(function (buffer) {
                    decodeBuffer(buffer, source.type);
                }).catch(function (error) {
                    reportError("Error while trying to load image blob.", error);
                });
                return null;
            }

            if (source instanceof ArrayBuffer || ArrayBuffer.isView(source)) {
                decodeBuffer(source, mimeType);
                return null;
            }

            if (typeof source === "string") {
                if (isValidationObjectUrl(source)) {
                    const blob = validationObjectUrls[source];
                    blobToArrayBuffer(blob).then(function (buffer) {
                        decodeBuffer(buffer, blob.type);
                    }).catch(function (error) {
                        reportError("Error while trying to load image object URL.", error);
                    });
                    return null;
                }

                if (source.indexOf("data:") === 0) {
                    imageFromDataUrl(source).then(onLoad).catch(function (error) {
                        reportError("Error while trying to load image data URL.", error);
                    });
                    return null;
                }

                originalLoadFile(source, function (data) {
                    decodeBuffer(data, mimeType);
                }, undefined, offlineProvider, true, function (request, error) {
                    reportError("Error while trying to load image: " + source, error);
                });
                return null;
            }

            if (source && typeof source._getNativeImageData === "function") {
                onLoad(source);
                return source;
            }

            if (source && typeof source.getCanvasTexture === "function") {
                onLoad(source);
                return source;
            }

            reportError("Unsupported image source for validation.", source);
            return null;
        }

        BABYLON.Tools.LoadImage = loadImageWithNativeCanvas;
        if (BABYLON.FileTools) {
            BABYLON.FileTools.LoadImage = loadImageWithNativeCanvas;
        }
        if (BABYLON.Engine) {
            BABYLON.Engine._FileToolsLoadImage = loadImageWithNativeCanvas;
        }
        if (BABYLON.ThinEngine) {
            BABYLON.ThinEngine._FileToolsLoadImage = loadImageWithNativeCanvas;
        }
        if (BABYLON.AbstractEngine) {
            BABYLON.AbstractEngine._FileToolsLoadImage = loadImageWithNativeCanvas;
        }
    }

    function getUnsupportedNativeWebGPUEffectMessage(effect, context) {
        if (!effect || !effect._engine || !effect._engine.isWebGPU || effect._shaderLanguage !== 0) {
            return "";
        }

        try {
            const pipelineContext = typeof effect.getPipelineContext === "function" ? effect.getPipelineContext() : effect._pipelineContext;
            if (!pipelineContext || pipelineContext.stages || pipelineContext.stage) {
                return "";
            }
        } catch (e) {
            return "";
        }

        const parts = [];
        if (context) {
            parts.push(context);
        }
        if (effect.name) {
            parts.push("effect=" + formatLogArgument(effect.name));
        }
        if (effect._key) {
            parts.push("key=" + formatLogArgument(effect._key));
        }

        return "BabylonJS requested a GLSL WebGPU effect without a compiled pipeline" +
            (parts.length > 0 ? " (" + parts.join(",") + ")" : "") +
            ". BabylonNative WebGPU does not ship the glslang/twgsl fallback. This BabylonJS path needs a WGSL implementation, a shaderLanguage=WGSL createEffect path, or a deliberate NativeWebGPU exclusion.";
    }

    function getEffectReadinessDetails(effect, context) {
        if (!effect) {
            return "effect=null";
        }

        const details = [];
        try {
            if (typeof effect._shaderLanguage !== "undefined") {
                details.push("shaderLanguage=" + effect._shaderLanguage);
            }
            if (effect.name) {
                details.push("name=" + formatLogArgument(effect.name));
            }
            if (effect._key) {
                details.push("key=" + formatLogArgument(effect._key));
            }
            if (typeof effect.isReady === "function") {
                details.push("effectReady=" + effect.isReady());
            }
        } catch (e) {
            details.push("effectReadyError=" + formatLogArgument(e));
        }

        try {
            if (typeof effect.getCompilationError === "function") {
                const error = effect.getCompilationError();
                if (error) {
                    details.push("compileError=" + formatLogArgument(error));
                }
            } else if (effect._compilationError) {
                details.push("compileError=" + formatLogArgument(effect._compilationError));
            }
        } catch (e) {
            details.push("compileErrorReadError=" + formatLogArgument(e));
        }

        try {
            const pipelineContext = typeof effect.getPipelineContext === "function" ? effect.getPipelineContext() : effect._pipelineContext;
            if (pipelineContext) {
                details.push("pipelineReady=" + !!pipelineContext.isReady);
                if (!pipelineContext.stages && !pipelineContext.stage) {
                    details.push("pipelineStages=false");
                    const unsupportedMessage = getUnsupportedNativeWebGPUEffectMessage(effect, context);
                    if (unsupportedMessage) {
                        details.push("nativeWebGPUError=" + formatLogArgument(unsupportedMessage));
                    }
                }
            } else {
                details.push("pipeline=null");
            }
        } catch (e) {
            details.push("pipelineError=" + formatLogArgument(e));
        }

        return details.join(",");
    }

    function getMaterialReadinessLabel(material, mesh, subMesh, hardwareInstancedRendering) {
        const className = material && typeof material.getClassName === "function" ? material.getClassName() : (material && material.constructor ? material.constructor.name : "Material");
        const name = material && (material.name || material.id) ? (material.name || material.id) : "(unnamed material)";
        const details = [name + ":" + className];

        try {
            if (material && typeof material.shaderLanguage !== "undefined") {
                details.push("shaderLanguage=" + material.shaderLanguage);
            }
        } catch (e) {
            details.push("shaderLanguageError=" + formatLogArgument(e));
        }

        try {
            const effect = subMesh && subMesh.effect ? subMesh.effect : (material && typeof material.getEffect === "function" ? material.getEffect() : null);
            const effectContext = "material=" + name + ",class=" + className + (mesh ? ",mesh=" + (mesh.name || mesh.id || "(unnamed mesh)") : "");
            const effectDetails = getEffectReadinessDetails(effect, effectContext);
            if (effectDetails) {
                details.push(effectDetails);
            }
        } catch (e) {
            details.push("effectError=" + formatLogArgument(e));
        }

        try {
            if (material && typeof material.isReadyForSubMesh === "function" && mesh && subMesh) {
                details.push("isReadyForSubMesh=" + material.isReadyForSubMesh(mesh, subMesh, hardwareInstancedRendering));
            } else if (material && typeof material.isReady === "function") {
                details.push("isReady=" + material.isReady(mesh, hardwareInstancedRendering));
            }
        } catch (e) {
            details.push("readyCheckError=" + formatLogArgument(e));
        }

        return details[0] + "{" + details.slice(1).join(",") + "}";
    }

    function appendMeshReadinessDiagnostics(scene, details) {
        const notReadyMeshes = [];
        const notReadySubMeshes = [];
        const engine = scene.getEngine && scene.getEngine();
        const meshes = scene.meshes || [];
        for (let i = 0; i < meshes.length && (notReadyMeshes.length < 8 || notReadySubMeshes.length < 8); ++i) {
            const mesh = meshes[i];
            if (!mesh || !mesh.subMeshes || mesh.subMeshes.length === 0) {
                continue;
            }

            let meshReady = true;
            try {
                meshReady = typeof mesh.isReady === "function" ? mesh.isReady(true) : true;
            } catch (e) {
                meshReady = false;
                if (notReadyMeshes.length < 8) {
                    notReadyMeshes.push((mesh.name || mesh.id || "(unnamed mesh)") + "{meshReadyError=" + formatLogArgument(e) + "}");
                }
            }

            if (!meshReady && notReadyMeshes.length < 8) {
                notReadyMeshes.push(mesh.name || mesh.id || "(unnamed mesh)");
            }

            const hardwareInstancedRendering = !!(mesh.hasThinInstances ||
                (mesh.getClassName && (mesh.getClassName() === "InstancedMesh" || mesh.getClassName() === "InstancedLinesMesh")) ||
                (engine && engine.getCaps && engine.getCaps().instancedArrays && mesh.instances && mesh.instances.length > 0));
            for (let subMeshIndex = 0; subMeshIndex < mesh.subMeshes.length && notReadySubMeshes.length < 8; ++subMeshIndex) {
                const subMesh = mesh.subMeshes[subMeshIndex];
                const material = subMesh && typeof subMesh.getMaterial === "function" ? subMesh.getMaterial() : (mesh.material || scene.defaultMaterial);
                if (!material) {
                    continue;
                }

                let ready = true;
                try {
                    if (material._storeEffectOnSubMeshes && typeof material.isReadyForSubMesh === "function") {
                        ready = material.isReadyForSubMesh(mesh, subMesh, hardwareInstancedRendering);
                    } else if (typeof material.isReady === "function") {
                        ready = material.isReady(mesh, hardwareInstancedRendering);
                    }
                } catch (e) {
                    ready = false;
                }

                if (!ready) {
                    notReadySubMeshes.push((mesh.name || mesh.id || "(unnamed mesh)") + "#" + subMeshIndex + ":" + getMaterialReadinessLabel(material, mesh, subMesh, hardwareInstancedRendering));
                }
            }
        }

        details.push("meshes=" + meshes.length + " notReady=[" + notReadyMeshes.join(",") + "]");
        details.push("subMeshesNotReady=[" + notReadySubMeshes.join(",") + "]");
    }

    function appendParticleReadinessDiagnostics(scene, details) {
        const notReadyParticles = [];
        const particleSystems = scene.particleSystems || [];
        for (let i = 0; i < particleSystems.length && notReadyParticles.length < 8; ++i) {
            const particleSystem = particleSystems[i];
            if (!particleSystem) {
                continue;
            }

            let ready = true;
            try {
                ready = typeof particleSystem.isReady === "function" ? particleSystem.isReady() : true;
            } catch (e) {
                ready = false;
            }

            if (ready) {
                continue;
            }

            const label = [particleSystem.name || particleSystem.id || "(unnamed particle system)"];
            try {
                label.push("emitter=" + !!particleSystem.emitter);
                label.push("textureReady=" + !!(particleSystem.particleTexture && particleSystem.particleTexture.isReady && particleSystem.particleTexture.isReady()));
                if (particleSystem._platform) {
                    label.push("updateBufferCreated=" + !!(particleSystem._platform.isUpdateBufferCreated && particleSystem._platform.isUpdateBufferCreated()));
                    label.push("updateBufferReady=" + !!(particleSystem._platform.isUpdateBufferReady && particleSystem._platform.isUpdateBufferReady()));
                    if (particleSystem._platform._updateComputeShader && particleSystem._platform._updateComputeShader._effect) {
                        label.push("updateEffect={" + getEffectReadinessDetails(particleSystem._platform._updateComputeShader._effect, "particleSystem=" + label[0] + ",stage=update") + "}");
                    }
                }
                if (typeof particleSystem._getWrapper === "function") {
                    const wrapper = particleSystem._getWrapper(particleSystem.blendMode);
                    if (wrapper && wrapper.effect) {
                        label.push("renderEffect={" + getEffectReadinessDetails(wrapper.effect, "particleSystem=" + label[0] + ",stage=render") + "}");
                    }
                }
            } catch (e) {
                label.push("diagnosticError=" + formatLogArgument(e));
            }

            notReadyParticles.push(label[0] + "{" + label.slice(1).join(",") + "}");
        }

        details.push("particleSystems=" + particleSystems.length + " notReady=[" + notReadyParticles.join(",") + "]");
    }

    function findUnsupportedNativeWebGPUEffectInScene(scene) {
        if (!scene) {
            return "";
        }

        const engine = scene.getEngine && scene.getEngine();
        if (!engine || !engine.isWebGPU) {
            return "";
        }

        const meshes = scene.meshes || [];
        for (let i = 0; i < meshes.length; ++i) {
            const mesh = meshes[i];
            const subMeshes = mesh && mesh.subMeshes ? mesh.subMeshes : [];
            for (let subMeshIndex = 0; subMeshIndex < subMeshes.length; ++subMeshIndex) {
                const subMesh = subMeshes[subMeshIndex];
                const material = subMesh && typeof subMesh.getMaterial === "function" ? subMesh.getMaterial() : (mesh.material || scene.defaultMaterial);
                const effect = subMesh && subMesh.effect ? subMesh.effect : (material && typeof material.getEffect === "function" ? material.getEffect() : null);
                const className = material && typeof material.getClassName === "function" ? material.getClassName() : (material && material.constructor ? material.constructor.name : "Material");
                const name = material && (material.name || material.id) ? (material.name || material.id) : "(unnamed material)";
                const message = getUnsupportedNativeWebGPUEffectMessage(
                    effect,
                    "material=" + name + ",class=" + className + ",mesh=" + (mesh.name || mesh.id || "(unnamed mesh)") + ",subMesh=" + subMeshIndex);
                if (message) {
                    return message;
                }
            }
        }

        const particleSystems = scene.particleSystems || [];
        for (let i = 0; i < particleSystems.length; ++i) {
            const particleSystem = particleSystems[i];
            if (!particleSystem) {
                continue;
            }

            const particleName = particleSystem.name || particleSystem.id || "(unnamed particle system)";
            try {
                if (particleSystem._platform && particleSystem._platform._updateComputeShader && particleSystem._platform._updateComputeShader._effect) {
                    const updateMessage = getUnsupportedNativeWebGPUEffectMessage(
                        particleSystem._platform._updateComputeShader._effect,
                        "particleSystem=" + particleName + ",stage=update");
                    if (updateMessage) {
                        return updateMessage;
                    }
                }
                if (typeof particleSystem._getWrapper === "function") {
                    const wrapper = particleSystem._getWrapper(particleSystem.blendMode);
                    if (wrapper && wrapper.effect) {
                        const renderMessage = getUnsupportedNativeWebGPUEffectMessage(
                            wrapper.effect,
                            "particleSystem=" + particleName + ",stage=render");
                        if (renderMessage) {
                            return renderMessage;
                        }
                    }
                }
            } catch (e) {
                // Diagnostic-only; readiness reporting will include more detail.
            }
        }

        return "";
    }

    function findUnsupportedNativeWebGPUEffectInSceneFamily(scene) {
        const mainMessage = findUnsupportedNativeWebGPUEffectInScene(scene);
        if (mainMessage) {
            return "main: " + mainMessage;
        }

        try {
            if (BABYLON.UtilityLayerRenderer) {
                const utilityLayers = [
                    BABYLON.UtilityLayerRenderer._DefaultUtilityLayer,
                    BABYLON.UtilityLayerRenderer._DefaultKeepDepthUtilityLayer
                ];
                const seenScenes = [];
                for (let i = 0; i < utilityLayers.length; ++i) {
                    const utilityLayer = utilityLayers[i];
                    const utilityScene = utilityLayer && utilityLayer.utilityLayerScene;
                    if (!utilityScene || seenScenes.indexOf(utilityScene) !== -1) {
                        continue;
                    }
                    seenScenes.push(utilityScene);
                    const message = findUnsupportedNativeWebGPUEffectInScene(utilityScene);
                    if (message) {
                        return "utilityLayer" + i + ": " + message;
                    }
                }
            }
        } catch (e) {
            // Diagnostic-only; readiness reporting will include more detail.
        }

        return "";
    }

    function getSceneReadinessDiagnostics(scene) {
        const details = [];
        if (!scene) {
            return "scene=null";
        }

        try {
            if (typeof scene.getWaitingItemsCount === "function") {
                details.push("waitingItems=" + scene.getWaitingItemsCount());
            }
        } catch (e) {
            details.push("waitingItemsError=" + formatLogArgument(e));
        }

        try {
            if (scene._pendingData) {
                details.push("pendingData=" + scene._pendingData.length);
                const pending = [];
                for (let i = 0; i < Math.min(scene._pendingData.length, 8); ++i) {
                    const item = scene._pendingData[i];
                    pending.push(item && item.constructor ? item.constructor.name : typeof item);
                }
                if (pending.length > 0) {
                    details.push("pendingTypes=[" + pending.join(",") + "]");
                }
            }
        } catch (e) {
            details.push("pendingDataError=" + formatLogArgument(e));
        }

        try {
            const notReadyTextures = [];
            const textures = scene.textures || [];
            for (let i = 0; i < textures.length && notReadyTextures.length < 8; ++i) {
                const texture = textures[i];
                let ready = true;
                if (texture && typeof texture.isReady === "function") {
                    ready = texture.isReady();
                }
                if (!ready) {
                    notReadyTextures.push(texture.name || texture.url || texture._url || "(unnamed texture)");
                }
            }
            details.push("textures=" + textures.length + " notReady=[" + notReadyTextures.join(",") + "]");
        } catch (e) {
            details.push("textureError=" + formatLogArgument(e));
        }

        try {
            appendMeshReadinessDiagnostics(scene, details);
        } catch (e) {
            details.push("meshMaterialError=" + formatLogArgument(e));
        }

        try {
            appendParticleReadinessDiagnostics(scene, details);
        } catch (e) {
            details.push("particleError=" + formatLogArgument(e));
        }

        if (lastConsoleError) {
            details.push("lastConsoleError=" + lastConsoleError);
        }

        return details.join(" ");
    }

    function getSceneFamilyReadinessDiagnostics(scene) {
        const diagnostics = ["main{" + getSceneReadinessDiagnostics(scene) + "}"];
        try {
            if (BABYLON.UtilityLayerRenderer) {
                const utilityLayers = [
                    BABYLON.UtilityLayerRenderer._DefaultUtilityLayer,
                    BABYLON.UtilityLayerRenderer._DefaultKeepDepthUtilityLayer
                ];
                const seenScenes = [];
                for (let i = 0; i < utilityLayers.length; ++i) {
                    const utilityLayer = utilityLayers[i];
                    const utilityScene = utilityLayer && utilityLayer.utilityLayerScene;
                    if (!utilityScene || seenScenes.indexOf(utilityScene) !== -1) {
                        continue;
                    }
                    seenScenes.push(utilityScene);
                    diagnostics.push("utilityLayer" + i + "{shouldRender=" + !!utilityLayer.shouldRender + " " + getSceneReadinessDiagnostics(utilityScene) + "}");
                }
            }
        } catch (e) {
            diagnostics.push("utilityLayerError=" + formatLogArgument(e));
        }
        return diagnostics.join(" ");
    }

    function disposeCurrentSceneForFailure() {
        try {
            if (currentScene) {
                currentScene.dispose();
                currentScene = null;
            }
        } catch (e) {
            console.error(e);
        }
        try {
            engine.stopRenderLoop();
            engine.setHardwareScalingLevel(1);
            engine.releaseEffects();
        } catch (e) {
            console.error(e);
        }
    }

    function primeDynamicTexturesForReadiness(scene) {
        if (!scene || !scene.textures) {
            return;
        }

        const primed = [];
        for (let i = 0; i < scene.textures.length; ++i) {
            const texture = scene.textures[i];
            if (!texture || typeof texture.update !== "function") {
                continue;
            }

            let ready = true;
            try {
                ready = typeof texture.isReady === "function" ? texture.isReady() : true;
            } catch (e) {
                ready = false;
            }

            if (ready) {
                continue;
            }

            const className = typeof texture.getClassName === "function" ? texture.getClassName() : "";
            if (className !== "DynamicTexture") {
                continue;
            }

            try {
                texture.update(false);
                primed.push(texture.name || "(unnamed dynamic texture)");
            } catch (e) {
                console.error("Dynamic texture readiness prime failed for " + (texture.name || "(unnamed)") + ": " + formatLogArgument(e));
            }
        }

        if (primed.length > 0) {
            console.log("Primed dynamic textures before executeWhenReady: " + primed.join(","));
        }
    }

    function startSceneReadinessRenderPump(scene, label, onFatalError) {
        let stopped = false;
        let frameCount = 0;
        const pumpLabel = label || "readiness";

        const pump = function () {
            if (stopped) {
                return;
            }
            if (!scene || scene.isDisposed === true || (typeof scene.isDisposed === "function" && scene.isDisposed())) {
                stopped = true;
                return;
            }

            try {
                if (scene.activeCamera && typeof scene.render === "function") {
                    scene.render();
                    frameCount++;
                }
                const unsupportedWebGPUEffect = findUnsupportedNativeWebGPUEffectInSceneFamily(scene);
                if (unsupportedWebGPUEffect) {
                    throw new Error("NATIVE_WEBGPU_UNSUPPORTED_EFFECT: " + unsupportedWebGPUEffect);
                }
            } catch (e) {
                stopped = true;
                console.error("Readiness render pump failed during " + pumpLabel + ": " + formatLogArgument(e));
                if (typeof onFatalError === "function") {
                    onFatalError(e);
                }
                return;
            }

            if (!stopped) {
                setTimeout(pump, 16);
            }
        };

        setTimeout(pump, 0);

        return {
            stop: function () {
                if (!stopped && frameCount > 0) {
                    console.log("Readiness render pump for " + pumpLabel + " rendered " + frameCount + " frame(s).");
                }
                stopped = true;
            },
            getFrameCount: function () {
                return frameCount;
            }
        };
    }

    function installWebGPUPreviousWorldBufferFrameOrderShim(engine) {
        if (!engine || !engine.isWebGPU || typeof engine.flushFramebuffer !== "function" || !BABYLON.Buffer) {
            return;
        }

        const bufferPrototype = BABYLON.Buffer.prototype;
        if (!bufferPrototype || bufferPrototype.__nativeValidationPreviousWorldFrameOrderInstalled) {
            return;
        }

        const originalCreateVertexBuffer = bufferPrototype.createVertexBuffer;
        const originalUpdateDirectly = bufferPrototype.updateDirectly;
        if (typeof originalCreateVertexBuffer !== "function" || typeof originalUpdateDirectly !== "function") {
            return;
        }

        bufferPrototype.createVertexBuffer = function (kind) {
            if (typeof kind === "string" && kind.indexOf("previousWorld") === 0) {
                this.__nativeValidationPreviousWorldBuffer = true;
            }
            return originalCreateVertexBuffer.apply(this, arguments);
        };

        bufferPrototype.updateDirectly = function () {
            if (this.__nativeValidationPreviousWorldBuffer && engine._currentRenderPass) {
                engine.flushFramebuffer();
            }
            return originalUpdateDirectly.apply(this, arguments);
        };

        bufferPrototype.__nativeValidationPreviousWorldFrameOrderInstalled = true;
    }

    function installEffectPreparationDiagnostics(engine) {
        if (!engine || !engine.isWebGPU || !BABYLON.Effect || !BABYLON.Effect.prototype || BABYLON.Effect.prototype.__nativeValidationEffectDiagnosticsInstalled) {
            return;
        }

        const originalProcessShaderCodeAsync = BABYLON.Effect.prototype._processShaderCodeAsync;
        if (typeof originalProcessShaderCodeAsync !== "function") {
            return;
        }

        BABYLON.Effect.prototype._processShaderCodeAsync = async function () {
            try {
                return await originalProcessShaderCodeAsync.apply(this, arguments);
            } catch (e) {
                console.error("WEBGPU_EFFECT_PREPARATION_FAILED: " + getEffectReadinessDetails(this, "effect-preparation") + " error=" + formatLogArgument(e));
                throw e;
            }
        };

        BABYLON.Effect.prototype.__nativeValidationEffectDiagnosticsInstalled = true;
    }

    function installWebGPUGui3DShaderLanguageShim(engine) {
        if (!engine || !engine.isWebGPU || !BABYLON.ShaderLanguage || engine.__nativeValidationGui3DShaderLanguageInstalled) {
            return;
        }

        const shaderStore = BABYLON.ShaderStore || BABYLON.Effect;
        if (!shaderStore || !shaderStore.ShadersStoreWGSL) {
            return;
        }

        if (!shaderStore.ShadersStoreWGSL.fluentVertexShader) {
            shaderStore.ShadersStoreWGSL.fluentVertexShader = `attribute position: vec3f;attribute normal: vec3f;attribute uv: vec2f;uniform world: mat4x4f;uniform viewProjection: mat4x4f;varying vUV: vec2f;
#ifdef BORDER
varying scaleInfo: vec2f;uniform borderWidth: f32;uniform scaleFactor: vec3f;
#endif
#ifdef HOVERLIGHT
varying worldPosition: vec3f;
#endif
@vertex
fn main(input: VertexInputs)->FragmentInputs {
vertexOutputs.vUV=vertexInputs.uv;
#ifdef BORDER
var scale: vec3f=uniforms.scaleFactor;var minScale: f32=min(min(scale.x,scale.y),scale.z);var maxScale: f32=max(max(scale.x,scale.y),scale.z);var minOverMiddleScale: f32=minScale/(scale.x+scale.y+scale.z-minScale-maxScale);var areaYZ: f32=scale.y*scale.z;var areaXZ: f32=scale.x*scale.z;var areaXY: f32=scale.x*scale.y;var scaledBorderWidth: f32=uniforms.borderWidth;
if (abs(vertexInputs.normal.x)==1.0) {scale.x=scale.y;scale.y=scale.z;if (areaYZ>areaXZ && areaYZ>areaXY) {scaledBorderWidth=scaledBorderWidth*minOverMiddleScale;}}
else if (abs(vertexInputs.normal.y)==1.0) {scale.x=scale.z;if (areaXZ>areaXY && areaXZ>areaYZ) {scaledBorderWidth=scaledBorderWidth*minOverMiddleScale;}}
else {if (areaXY>areaYZ && areaXY>areaXZ) {scaledBorderWidth=scaledBorderWidth*minOverMiddleScale;}}
var scaleRatio: f32=min(scale.x,scale.y)/max(scale.x,scale.y);
if (scale.x>scale.y) {vertexOutputs.scaleInfo=vec2f(1.0-(scaledBorderWidth*scaleRatio),1.0-scaledBorderWidth);}
else {vertexOutputs.scaleInfo=vec2f(1.0-scaledBorderWidth,1.0-(scaledBorderWidth*scaleRatio));}
#endif
var worldPos: vec4f=uniforms.world*vec4f(vertexInputs.position,1.0);
#ifdef HOVERLIGHT
vertexOutputs.worldPosition=worldPos.xyz;
#endif
vertexOutputs.position=uniforms.viewProjection*worldPos;
}`;
        }

        if (!shaderStore.ShadersStoreWGSL.fluentPixelShader) {
            shaderStore.ShadersStoreWGSL.fluentPixelShader = `varying vUV: vec2f;uniform albedoColor: vec4f;
#ifdef INNERGLOW
uniform innerGlowColor: vec4f;
#endif
#ifdef BORDER
varying scaleInfo: vec2f;uniform edgeSmoothingValue: f32;uniform borderMinValue: f32;
#endif
#ifdef HOVERLIGHT
varying worldPosition: vec3f;uniform hoverPosition: vec3f;uniform hoverColor: vec4f;uniform hoverRadius: f32;
#endif
#ifdef TEXTURE
var albedoSamplerSampler: sampler;var albedoSampler: texture_2d<f32>;uniform textureMatrix: mat4x4f;
#endif
@fragment
fn main(input: FragmentInputs)->FragmentOutputs {
var albedo: vec3f=uniforms.albedoColor.rgb;var alpha: f32=uniforms.albedoColor.a;
#ifdef TEXTURE
let finalUV: vec2f=(uniforms.textureMatrix*vec4f(input.vUV,1.0,0.0)).xy;albedo=textureSample(albedoSampler,albedoSamplerSampler,finalUV).rgb;
#endif
#ifdef HOVERLIGHT
let pointToHover: f32=(1.0-clamp(length(uniforms.hoverPosition-input.worldPosition)/uniforms.hoverRadius,0.0,1.0))*uniforms.hoverColor.a;albedo=clamp(albedo+uniforms.hoverColor.rgb*pointToHover,vec3f(0.0),vec3f(1.0));
#else
let pointToHover: f32=1.0;
#endif
#ifdef BORDER
let borderPower: f32=10.0;let inverseBorderPower: f32=1.0/borderPower;var borderColor: vec3f=albedo*borderPower;let distanceToEdge: vec2f=abs(input.vUV-vec2f(0.5))*2.0;let borderValue: f32=max(smoothstep(input.scaleInfo.x-uniforms.edgeSmoothingValue,input.scaleInfo.x+uniforms.edgeSmoothingValue,distanceToEdge.x),smoothstep(input.scaleInfo.y-uniforms.edgeSmoothingValue,input.scaleInfo.y+uniforms.edgeSmoothingValue,distanceToEdge.y));borderColor=borderColor*borderValue*max(uniforms.borderMinValue*inverseBorderPower,pointToHover);albedo=albedo+borderColor;alpha=max(alpha,borderValue);
#endif
#ifdef INNERGLOW
var uvGlow: vec2f=(input.vUV-vec2f(0.5))*(uniforms.innerGlowColor.a*2.0);uvGlow=uvGlow*uvGlow;uvGlow=uvGlow*uvGlow;albedo=albedo+mix(vec3f(0.0),uniforms.innerGlowColor.rgb,uvGlow.x+uvGlow.y);
#endif
fragmentOutputs.color=vec4f(albedo,alpha);
}`;
        }

        if (!shaderStore.ShadersStoreWGSL.handleVertexShader) {
            shaderStore.ShadersStoreWGSL.handleVertexShader = `attribute position: vec3f;uniform positionOffset: vec3f;uniform worldViewProjection: mat4x4f;uniform scale: f32;
@vertex
fn main(input: VertexInputs)->FragmentInputs {
let vPos: vec4f=vec4f((vertexInputs.position+uniforms.positionOffset)*uniforms.scale,1.0);
vertexOutputs.position=uniforms.worldViewProjection*vPos;
}`;
        }

        if (!shaderStore.ShadersStoreWGSL.handlePixelShader) {
            shaderStore.ShadersStoreWGSL.handlePixelShader = `uniform color: vec3f;
@fragment
fn main(input: FragmentInputs)->FragmentOutputs {
fragmentOutputs.color=vec4f(uniforms.color,1.0);
}`;
        }

        if (!shaderStore.ShadersStoreWGSL.fluentBackplateVertexShader) {
            shaderStore.ShadersStoreWGSL.fluentBackplateVertexShader = `attribute position: vec3f;attribute normal: vec3f;uniform world: mat4x4f;uniform viewProjection: mat4x4f;varying vUV: vec2f;
@vertex
fn main(input: VertexInputs)->FragmentInputs {
let worldPos: vec4f=uniforms.world*vec4f(vertexInputs.position,1.0);
vertexOutputs.position=uniforms.viewProjection*worldPos;
vertexOutputs.vUV=vertexInputs.position.xy*2.0+vec2f(0.5);
}`;
        }

        if (!shaderStore.ShadersStoreWGSL.fluentBackplatePixelShader) {
            shaderStore.ShadersStoreWGSL.fluentBackplatePixelShader = `varying vUV: vec2f;uniform _Line_Width_: f32;uniform _Base_Color_: vec4f;uniform _Line_Color_: vec4f;uniform _Fade_Out_: f32;
@fragment
fn main(input: FragmentInputs)->FragmentOutputs {
let edge: vec2f=abs(input.vUV-vec2f(0.5))*2.0;
let distanceToEdge: f32=max(edge.x,edge.y);
let lineWidth: f32=max(uniforms._Line_Width_*6.0,0.01);
let border: f32=smoothstep(1.0-lineWidth,1.0,distanceToEdge);
var color: vec4f=mix(uniforms._Base_Color_,uniforms._Line_Color_,border);
color.a=color.a*uniforms._Fade_Out_;
fragmentOutputs.color=color;
}`;
        }

        const webGpuGui3DShaders = {
            fluent: true,
            fluentBackplate: true,
            handle: true
        };
        const originalCreateEffect = engine.createEffect;
        if (typeof originalCreateEffect !== "function") {
            return;
        }

        engine.createEffect = function (baseName) {
            const args = Array.prototype.slice.call(arguments);
            const vertex = typeof baseName === "string" ? baseName : (baseName && (baseName.vertexToken || baseName.vertexSource || baseName.vertexElement || baseName.vertex));
            const fragment = typeof baseName === "string" ? baseName : (baseName && (baseName.fragmentToken || baseName.fragmentSource || baseName.fragmentElement || baseName.fragment));
            if (vertex && fragment && vertex === fragment && webGpuGui3DShaders[vertex]) {
                if (args[1] && typeof args[1] === "object") {
                    args[1] = Object.assign({}, args[1], { shaderLanguage: BABYLON.ShaderLanguage.WGSL });
                } else {
                    args[9] = BABYLON.ShaderLanguage.WGSL;
                }
            }
            return originalCreateEffect.apply(this, args);
        };

        engine.__nativeValidationGui3DShaderLanguageInstalled = true;
    }

    function installSceneReadinessShims() {
        if (!BABYLON.Scene || BABYLON.Scene.prototype.__nativeValidationReadinessPumpInstalled) {
            return;
        }

        const originalWhenReadyAsync = BABYLON.Scene.prototype.whenReadyAsync;
        if (typeof originalWhenReadyAsync === "function") {
            BABYLON.Scene.prototype.whenReadyAsync = function () {
                const scene = this;
                const readinessPump = startSceneReadinessRenderPump(scene, "scene.whenReadyAsync");
                try {
                    return originalWhenReadyAsync.apply(scene, arguments).then(function (result) {
                        readinessPump.stop();
                        return result;
                    }, function (error) {
                        readinessPump.stop();
                        throw error;
                    });
                } catch (e) {
                    readinessPump.stop();
                    throw e;
                }
            };
        }

        BABYLON.Scene.prototype.__nativeValidationReadinessPumpInstalled = true;
    }

    function createNativeCanvasForWebGPU() {
        if (typeof navigator === "undefined" || !navigator.gpu || typeof navigator.gpu._createCanvasContext !== "function") {
            return null;
        }

        const width = testWidth;
        const height = testHeight;
        const gpuContext = navigator.gpu._createCanvasContext();
        const canvas = {
            style: {
                width: width + "px",
                height: height + "px"
            },
            ownerDocument: (typeof document !== "undefined" ? document : undefined),
            width: width,
            height: height,
            clientWidth: width,
            clientHeight: height,
            addEventListener: function () { },
            removeEventListener: function () { },
            setAttribute: function () { },
            focus: function () { },
            requestPointerLock: function () { },
            requestFullscreen: function () { return Promise.resolve(); },
            getBoundingClientRect: function () {
                return {
                    x: 0,
                    y: 0,
                    top: 0,
                    left: 0,
                    right: width,
                    bottom: height,
                    width: width,
                    height: height
                };
            },
            getContext: function (contextName) {
                if (contextName === "webgpu") {
                    gpuContext.canvas = canvas;
                    return gpuContext;
                }

                return null;
            }
        };
        return canvas;
    }

    async function createValidationEngineAsync() {
        if (!globalThis.__babylonNativeValidationUseWebGPU) {
            return {
                engine: new BABYLON.NativeEngine(),
                canvas: window
            };
        }

        if (typeof BABYLON.WebGPUEngine !== "function") {
            throw new Error("BABYLON.WebGPUEngine is not available. Ensure babylon.max.js is loaded.");
        }

        const webgpuCanvas = createNativeCanvasForWebGPU();
        if (!webgpuCanvas) {
            throw new Error("WebGPU requested but no native canvas context is available.");
        }

        const webgpuEngine = new BABYLON.WebGPUEngine(webgpuCanvas, {
            antialias: false,
            adaptToDeviceRatio: false
        });
        await webgpuEngine.initAsync();
        return {
            engine: webgpuEngine,
            canvas: webgpuCanvas
        };
    }

    installValidationBrowserShims();

    const validationEngine = await createValidationEngineAsync();
    const engine = validationEngine.engine;
    engine.getCaps().parallelShaderCompile = undefined;
    installEffectPreparationDiagnostics(engine);
    installWebGPUPreviousWorldBufferFrameOrderShim(engine);
    if (engine.isWebGPU && typeof navigator !== "undefined" && navigator.gpu && typeof navigator.gpu._backendStats === "function") {
        try {
            const stats = navigator.gpu._backendStats();
            if (stats && stats.placeholderRendererActive) {
                console.warn("WEBGPU_PLACEHOLDER_RENDER_PATH: Native WebGPU presentation is still using the debug cube shim; screenshot comparisons validate readiness only until render-pass command execution is implemented.");
            }
        } catch (e) {
            // Stats are diagnostic-only.
        }
    }
    if (engine.isWebGPU && BABYLON.NodeMaterial) {
        BABYLON.NodeMaterial.UseNativeShaderLanguageOfEngine = true;
        if (BABYLON.ShaderLanguage) {
            BABYLON.NodeMaterial.DefaultShaderLanguage = BABYLON.ShaderLanguage.WGSL;
        }
    }
    installSceneReadinessShims();
    installValidationImageLoadingShim();

    // Broaden Babylon's default retry strategy for the test framework: in addition to
    // network drops (status 0, the default trigger), also retry transient HTTP errors
    // (5xx) and rate limits (429). Applies to every BABYLON.Tools.LoadFile request
    // including the snippet fetches in loadPG below and the texture/asset loads
    // initiated from inside each playground's createScene().
    BABYLON.Tools.DefaultRetryStrategy = function (url, request, retryIndex) {
        const maxRetries = 5;
        if (retryIndex >= maxRetries) {
            return -1;
        }
        if (url.indexOf("file:") !== -1) {
            return -1;
        }
        if (request.status === 0 ||
            request.status === 429 ||
            (request.status >= 500 && request.status < 600)) {
            return Math.pow(2, retryIndex) * 500;
        }
        return -1;
    };

    engine.getRenderingCanvas = function () {
        return validationEngine.canvas;
    }

    engine.getInputElement = function () {
        return 0;
    }

    const canvas = validationEngine.canvas;

    // Random replacement
    let seed = 1;
    Math.random = function () {
        const x = Math.sin(seed++) * 10000;
        return x - Math.floor(x);
    }

    function compare(test, renderData, referenceImage, threshold, errorRatio) {
        const referenceData = TestUtils.getImageData(referenceImage);
        if (referenceData.length != renderData.length) {
            throw new Error(`Reference data length (${referenceData.length}) must match render data length (${renderData.length})`);
        }

        const size = renderData.length;
        let differencesCount = 0;

        for (let index = 0; index < size; index += 4) {
            if (Math.abs(renderData[index] - referenceData[index]) < threshold &&
                Math.abs(renderData[index + 1] - referenceData[index + 1]) < threshold &&
                Math.abs(renderData[index + 2] - referenceData[index + 2]) < threshold) {
                continue;
            }

            if (differencesCount === 0) {
                console.log(`First pixel off at ${index}: Value: (${renderData[index]}, ${renderData[index + 1]}, ${renderData[index + 2]}) - Expected: (${referenceData[index]}, ${referenceData[index + 1]}, ${referenceData[index + 2]}) `);
            }

            referenceData[index] = 255;
            referenceData[index + 1] *= 0.5;
            referenceData[index + 2] *= 0.5;
            differencesCount++;
        }

        if (differencesCount) {
            console.log("Pixel difference: " + differencesCount + " pixels.");
        } else {
            console.log("No pixel difference!");
        }

        const error = (differencesCount * 100) / (size / 4) > errorRatio;

        const width = testWidth / engine.getHardwareScalingLevel();
        const height = testHeight / engine.getHardwareScalingLevel();

        if (error) {
            TestUtils.writePNG(referenceData, width, height, TestUtils.getOutputDirectory() + "/Errors/" + test.referenceImage);
        }
        if (saveResult || error) {
            TestUtils.writePNG(renderData, width, height, TestUtils.getOutputDirectory() + "/Results/" + test.referenceImage);
        }
        return error;
    }

    function saveRenderedResult(test, renderData) {
        const width = testWidth / engine.getHardwareScalingLevel();
        const height = testHeight / engine.getHardwareScalingLevel();
        TestUtils.writePNG(renderData, width, height, TestUtils.getOutputDirectory() + "/Results/" + test.referenceImage);
        return false; // no error
    }

    function evaluateScreenshot(test, screenshot, referenceImage, done, compareFunction) {
        let testRes = true;

        if (!test.onlyVisual) {

            const defaultErrorRatio = 2.5;

            if (compareFunction(test, screenshot, referenceImage, test.threshold || 25, test.errorRatio || defaultErrorRatio)) {
                testRes = false;
                console.log("Test '" + (test.title || "(unnamed)") + "' failed");
                logNativeWebGPUStats("after failed " + (test.title || "(unnamed)"));
                if (engine.isWebGPU) {
                    console.error("SCENE_RENDER_MISMATCH_DIAGNOSTICS: " + getSceneFamilyReadinessDiagnostics(currentScene));
                }
            } else {
                testRes = true;
                console.log("Test '" + (test.title || "(unnamed)") + "' validated");
            }
        }

        currentScene.dispose();
        currentScene = null;
        engine.setHardwareScalingLevel(1);

        // This is necessary because of https://github.com/BabylonJS/Babylon.js/pull/15217 so that each test starts fresh.
        engine.releaseEffects();

        done(testRes);
    }

    function evaluate(test, referenceImage, done, compareFunction) {
        TestUtils.getFrameBufferData(function (screenshot) {
            evaluateScreenshot(test, screenshot, referenceImage, done, compareFunction);
        });
    }

    function processCurrentScene(test, renderImage, done, compareFunction) {
        currentScene.useConstantAnimationDeltaTime = true;
        // Frame at which to read back the framebuffer & validate. This is the
        // test's renderCount (default 1) and determines pass/fail. NOT shifted
        // by --capture.
        const compareFrame = test.renderCount || 1;
        // Frame at which to call TestUtils.captureNextFrame(), or 0 if no
        // capture is requested. CLI --capture=N takes precedence over the
        // per-test "capture" config flag; the legacy per-test flag triggers
        // at compareFrame.
        const captureFrame = cliCaptureFrame > 0
            ? cliCaptureFrame
            : (test.capture ? compareFrame : 0);
        // Stop after this many frames. With --capture we keep rendering past
        // compareFrame so RenderDoc can finalize the .rdc.
        const stopFrame = captureFrame > 0
            ? Math.max(compareFrame, captureFrame + POST_CAPTURE_FRAMES)
            : compareFrame;

        let frameIndex = 0;
        let stopped = false;
        let pendingScreenshot = null;
        let readbackRequested = false;
        let evaluated = false;
        let readinessTimer = null;
        let readinessPump = null;

        const runEvaluation = function (screenshot) {
            if (evaluated) {
                return;
            }
            if (readinessTimer !== null) {
                clearTimeout(readinessTimer);
                readinessTimer = null;
            }
            if (readinessPump !== null) {
                readinessPump.stop();
                readinessPump = null;
            }
            evaluated = true;
            evaluateScreenshot(test, screenshot, renderImage, done, compareFunction);
        };

        const requestScreenshot = function () {
            if (evaluated || readbackRequested) {
                return;
            }
            readbackRequested = true;

            const requestFramebufferData = function () {
                if (evaluated) {
                    return;
                }
                TestUtils.getFrameBufferData(function (data) {
                    if (stopped) {
                        runEvaluation(data);
                    } else {
                        pendingScreenshot = data;
                    }
                });
            };

            if (engine.isWebGPU) {
                waitForValidationWebGPUQueue().then(requestFramebufferData).catch(function (error) {
                    if (evaluated) {
                        return;
                    }
                    evaluated = true;
                    stopped = true;
                    engine.stopRenderLoop();
                    console.error("WEBGPU_QUEUE_WAIT_FAILED: " + formatLogArgument(error));
                    disposeCurrentSceneForFailure();
                    failTest(done);
                });
            } else {
                requestFramebufferData();
            }
        };

        readinessTimer = setTimeout(function () {
            if (evaluated) {
                return;
            }

            evaluated = true;
            stopped = true;
            if (readinessPump !== null) {
                readinessPump.stop();
                readinessPump = null;
            }
            console.error(
                "SCENE_READY_TIMEOUT: Test '" + (test.title || "(unnamed)") +
                "' did not reach executeWhenReady within " + sceneReadyTimeoutMs + "ms. " +
                getSceneReadinessDiagnostics(currentScene));
            disposeCurrentSceneForFailure();
            failTest(done);
        }, sceneReadyTimeoutMs);

        primeDynamicTexturesForReadiness(currentScene);
        readinessPump = startSceneReadinessRenderPump(currentScene, "executeWhenReady for " + (test.title || "(unnamed)"), function (error) {
            if (evaluated) {
                return;
            }
            evaluated = true;
            stopped = true;
            if (readinessTimer !== null) {
                clearTimeout(readinessTimer);
                readinessTimer = null;
            }
            if (readinessPump !== null) {
                readinessPump.stop();
                readinessPump = null;
            }
            const errorText = formatLogArgument(error);
            const failurePrefix = errorText.indexOf("NATIVE_WEBGPU_UNSUPPORTED_EFFECT") !== -1
                ? "NATIVE_WEBGPU_UNSUPPORTED_EFFECT_READY_FAILED"
                : "SCENE_READY_FAILED";
            console.error(errorText);
            console.error(failurePrefix + ": Test '" + (test.title || "(unnamed)") + "' failed during readiness. " + getSceneFamilyReadinessDiagnostics(currentScene));
            disposeCurrentSceneForFailure();
            failTest(done);
        });

        currentScene.executeWhenReady(function () {
            if (evaluated) {
                return;
            }
            if (readinessPump !== null && typeof readinessPump.getFrameCount === "function") {
                frameIndex = readinessPump.getFrameCount();
                if (frameIndex > 0) {
                    console.log("Counting " + frameIndex + " readiness render pump frame(s) toward renderCount for " + (test.title || "(unnamed)") + ".");
                }
            }
            if (readinessPump !== null) {
                readinessPump.stop();
                readinessPump = null;
            }
            if (readinessTimer !== null) {
                clearTimeout(readinessTimer);
                readinessTimer = null;
            }
            if (currentScene.activeCamera && currentScene.activeCamera.useAutoRotationBehavior) {
                currentScene.activeCamera.useAutoRotationBehavior = false;
            }
            engine.runRenderLoop(function () {
                try {
                    frameIndex++;

                    if (captureFrame > 0 && frameIndex === captureFrame && TestUtils.captureNextFrame) {
                        TestUtils.captureNextFrame();
                    }

                    currentScene.render();

                    if (frameIndex >= compareFrame && !readbackRequested) {
                        requestScreenshot();
                    }

                    if (frameIndex >= stopFrame && !stopped && (!readbackRequested || pendingScreenshot !== null)) {
                        stopped = true;
                        engine.stopRenderLoop();
                        if (pendingScreenshot !== null) {
                            // Defer dispose to next tick so it runs outside
                            // this runRenderLoop iteration.
                            const data = pendingScreenshot;
                            pendingScreenshot = null;
                            setTimeout(function () { runEvaluation(data); }, 0);
                        }
                    }
                }
                catch (e) {
                    if (readinessTimer !== null) {
                        clearTimeout(readinessTimer);
                        readinessTimer = null;
                    }
                    if (readinessPump !== null) {
                        readinessPump.stop();
                        readinessPump = null;
                    }
                    evaluated = true;
                    stopped = true;
                    console.error(e);
                    disposeCurrentSceneForFailure();
                    failTest(done);
                }
            });
        }, true);
    }

    function loadPlayground(test, done, referenceImage, compareFunction) {
        if (test.sceneFolder) {
            BABYLON.SceneLoader.Load(config.root + test.sceneFolder, test.sceneFilename, engine, function (newScene) {
                currentScene = newScene;
                processCurrentScene(test, referenceImage, done, compareFunction);
            },
                null,
                function (loadedScene, msg) {
                    console.error(msg);
                    failTest(done);
                });
        }
        else if (test.playgroundId) {
            if (test.playgroundId[0] !== "#" || test.playgroundId.indexOf("#", 1) === -1) {
                test.playgroundId += "#0";
            }

            const snippetUrl = "https://snippet.babylonjs.com";
            const pgRoot = "https://playground.babylonjs.com";

            const loadPG = function () {
                const url = snippetUrl + test.playgroundId.replace(/#/g, "/");
                BABYLON.Tools.LoadFile(
                    url,
                    function (responseText) {
                        try {
                            const snippet = JSON.parse(responseText);
                            let code = JSON.parse(snippet.jsonPayload).code.toString();

                            // Check if this is a v2 manifest and extract the entry file's code
                            // TODO: Handle multi-file playgrounds
                            try {
                                const manifestPayload = JSON.parse(code);
                                if (manifestPayload.v === 2) {
                                    code = manifestPayload.files[manifestPayload.entry]
                                        .replace(/export +default +/g, "")
                                        .replace(/export +/g, "");
                                }
                            } catch (e) {
                                // Not a manifest, proceed as usual
                            }

                            code = code
                                .replace(/"\/textures\//g, '"' + pgRoot + "/textures/")
                                .replace(/'\/textures\//g, "'" + pgRoot + "/textures/")
                                .replace(/"textures\//g, '"' + pgRoot + "/textures/")
                                .replace(/'textures\//g, "'" + pgRoot + "/textures/")
                                .replace(/\/scenes\//g, pgRoot + "/scenes/")
                                .replace(/"scenes\//g, '"' + pgRoot + "/scenes/")
                                .replace(/'scenes\//g, "'" + pgRoot + "/scenes/")
                                .replace(/"\.\.\/\.\.https/g, '"' + "https")
                                .replace("http://", "https://");

                            if (test.replace) {
                                const split = test.replace.split(",");
                                for (let i = 0; i < split.length; i += 2) {
                                    const source = split[i].trim();
                                    const destination = split[i + 1].trim();
                                    code = code.replace(source, destination);
                                }
                            }

                            currentScene = eval(code + "\r\ncreateScene(engine)");

                            if (currentScene.then) {
                                // Handle if createScene returns a promise
                                currentScene.then(function (scene) {
                                    currentScene = scene;
                                    processCurrentScene(test, referenceImage, done, compareFunction);
                                }).catch(function (e) {
                                    console.error(e);
                                    failTest(done);
                                });
                            } else {
                                // Handle if createScene returns a scene
                                processCurrentScene(test, referenceImage, done, compareFunction);
                            }
                        }
                        catch (e) {
                            console.error("Failed to evaluate playground snippet " + test.playgroundId + ": " + e);
                            failTest(done);
                        }
                    },
                    undefined,  // onProgress
                    undefined,  // database
                    false,      // useArrayBuffer (snippet response is JSON text)
                    function (request, exception) {
                        const status = request ? (request.status + " " + request.statusText) : "no response";
                        console.error("Failed to load playground snippet " + test.playgroundId + " after retries: " + status);
                        if (exception) {
                            console.error(exception);
                        }
                        failTest(done);
                    }
                );
            }
            loadPG();
        } else {
            // Fix references
            if (test.specificRoot) {
                BABYLON.Tools.BaseUrl = config.root + test.specificRoot;
            }

            const request = new XMLHttpRequest();
            request.open('GET', config.root + test.scriptToRun, true);

            request.onreadystatechange = function () {
                if (request.readyState === 4) {
                    try {
                        request.onreadystatechange = null;

                        let scriptToRun = request.responseText.replace(/..\/..\/assets\//g, config.root + "/Assets/");
                        scriptToRun = scriptToRun.replace(/..\/..\/Assets\//g, config.root + "/Assets/");
                        scriptToRun = scriptToRun.replace(/\/assets\//g, config.root + "/Assets/");

                        if (test.replace) {
                            const split = test.replace.split(",");
                            for (let i = 0; i < split.length; i += 2) {
                                const source = split[i].trim();
                                const destination = split[i + 1].trim();
                                scriptToRun = scriptToRun.replace(source, destination);
                            }
                        }

                        if (test.replaceUrl) {
                            const split = test.replaceUrl.split(",");
                            for (let i = 0; i < split.length; i++) {
                                const source = split[i].trim();
                                const regex = new RegExp(source, "g");
                                scriptToRun = scriptToRun.replace(regex, config.root + test.rootPath + source);
                            }
                        }

                        currentScene = eval(scriptToRun + test.functionToCall + "(engine)");
                        processCurrentScene(test, referenceImage, done, compareFunction);
                    }
                    catch (e) {
                        console.error(e);
                        failTest(done);
                    }
                }
            };
            request.onerror = function () {
                console.error("Network error during test load.");
                failTest(done);
            }

            request.send(null);
        }
    }
    function runTest(index, done) {
        if (index >= config.tests.length) {
            done(false);
        }

        const test = config.tests[index];
        const testInfo = "Running " + test.title;
        console.log(testInfo);
        TestUtils.setTitle(testInfo);

        seed = 1;

        if (generateReferences) {
            loadPlayground(test, done, undefined, saveRenderedResult);
        } else {
            // Config validation: missing 'referenceImage' field is a permanent
            // catalog error (not a runtime asset-missing case), so short-circuit
            // before issuing the load. onlyVisual tests skip pixel comparison
            // so they don't need the reference image to exist.
            if (!test.onlyVisual && !test.referenceImage) {
                console.error("MISSING_REFERENCE_IMAGE: Test '" + (test.title || "(unnamed)") +
                              "' has no 'referenceImage' field in config.json - cannot run pixel comparison.");
                missingRefCount++;
                failTest(done);
                return;
            }

            // run test and image comparison
            const url = "app:///ReferenceImages/" + test.referenceImage;

            const onLoadFileError = function (request, exception) {
                // Reference-image load failures (missing file on disk, etc.)
                // arrive here via JsRuntimeHost's XHR error event +
                // BABYLON.Tools.LoadFile's onLoadFileError callback. Tag with
                // MISSING_REFERENCE_IMAGE: so CI greps still match.
                console.error("MISSING_REFERENCE_IMAGE: Test '" + (test.title || "(unnamed)") +
                              "' failed to load reference at " + url + ". " +
                              (exception ? exception : "(no exception details)"));
                missingRefCount++;
                failTest(done);
            };

            const onload = function (data, responseURL) {
                if (typeof (data) === "string") {
                    throw new Error("Decode Image from string data not yet implemented.");
                }

                const referenceImage = TestUtils.decodeImage(data);
                loadPlayground(test, done, referenceImage, compare);
            };

            BABYLON.Tools.LoadFile(url, onload, undefined, undefined, /*useArrayBuffer*/true, onLoadFileError);
        }
    }

    const xhr = new XMLHttpRequest();
    xhr.open("GET", "app:///Scripts/config.json", true);

    xhr.addEventListener("readystatechange", function () {
        if (xhr.status === 200) {
            config = JSON.parse(xhr.responseText);

            if (listTests) {
                // Canonical TSV: index<TAB>title<TAB>referenceImage<TAB>exclusionReason.
                // exclusionReason reflects config state (ignores --include-excluded)
                // so the listing is the same regardless of run flags.
                for (let i = 0; i < config.tests.length; ++i) {
                    const t = config.tests[i];
                    const reason = getExclusionReason(t) || "";
                    console.log(i + "\t" + (t.title || "") + "\t" + (t.referenceImage || "") + "\t" + reason);
                }
                engine.dispose();
                TestUtils.exit(0);
                return;
            }

            // Run tests
            const recursiveRunTest = function (i) {
                // Skip filtered-out tests cheaply (don't count toward --once
                // and don't re-init the engine).
                //
                // Skipped tests (excludeFromAutomaticTesting / onlyVisual /
                // excludedGraphicsApis) are logged loudly when a filter is
                // active so the user sees that --test "X" matched but was
                // skipped. Filter mismatches stay silent to avoid noise on
                // unfiltered runs.
                while (i < config.tests.length) {
                    const t = config.tests[i];
                    const matchesFilter = shouldRunTest(t, i);
                    if (!matchesFilter) {
                        i++;
                        continue;
                    }
                    const reason = getSkipReason(t);
                    if (reason !== null) {
                        console.log("Skipping '" + (t.title || "(unnamed)") + "' -- " + reason);
                        skippedCount++;
                        i++;
                        continue;
                    }
                    break;
                }
                if (i >= config.tests.length) {
                    logRunSummary();
                    engine.dispose();
                    TestUtils.exit(failedCount > 0 ? -1 : 0);
                    return;
                }
                const currentTitle = config.tests[i].title || "(unnamed)";
                runTest(i, function (status) {
                    ranCount++;
                    if (!status) {
                        failedCount++;
                        // failTest() already triggered the debugger before
                        // reaching this callback; no second `debugger` here.
                        logRunSummary();
                        TestUtils.exit(-1);
                        return;
                    }
                    passedCount++;
                    i++;
                    if (justOnce || i >= config.tests.length) {
                        logRunSummary();
                        engine.dispose();
                        TestUtils.exit(0);
                        return;
                    }
                    // Defer next iteration to avoid blowing Chakra's
                    // recursion stack on long test lists.
                    setTimeout(function () { recursiveRunTest(i); }, 0);
                });
            }

            recursiveRunTest(0);
        }
    }, false);


    BABYLON.Tools.LoadFile("app:///Scripts/RobotoSlab.ttf", (data) => {
        _native.Canvas.loadTTFAsync("droidsans", data).then(function () {
            _native.RootUrl = "https://playground.babylonjs.com";
            console.log("Starting");
            TestUtils.setTitle("Starting Native Validation Tests");
            TestUtils.updateSize(testWidth, testHeight);
            xhr.send();
        });
    }, undefined, undefined, true);
})().catch(function (error) {
    console.error(error);
    if (typeof TestUtils !== "undefined" && TestUtils && typeof TestUtils.exit === "function") {
        TestUtils.exit(-1);
    }
});
