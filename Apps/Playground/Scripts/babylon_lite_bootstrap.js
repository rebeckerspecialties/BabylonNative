(function () {
    "use strict";

    var FRAMEBUFFER_WIDTH = 1280;
    var FRAMEBUFFER_HEIGHT = 720;
    var elementsById = Object.create(null);

    globalThis.__babylonPlaygroundSelfManagedScript = true;
    globalThis.window = globalThis;
    globalThis.window.innerWidth = FRAMEBUFFER_WIDTH;
    globalThis.window.innerHeight = FRAMEBUFFER_HEIGHT;
    globalThis.window.location = globalThis.window.location || { href: "app:///", search: "" };

    var originalFetch = globalThis.fetch;
    if (typeof originalFetch === "function") {
        globalThis.fetch = function (input, init) {
            if (typeof input === "string" && !/^[a-z][a-z0-9+.-]*:/i.test(input)) {
                input = input.charAt(0) === "/" ? "app://" + input : "app:///" + input;
            }
            return originalFetch(input, init);
        };
    }

    var state = globalThis.__babylonLiteValidationState = {
        consoleErrors: [],
        device: null,
        frameTimerIntervalId: null,
        startedAt: performance.now()
    };

    var originalConsoleError = console.error;
    console.error = function () {
        var values = Array.prototype.map.call(arguments, function (value) {
            if (value && (value.message || value.stack)) {
                var message = value.message ? String(value.message) : "";
                var summary = message ? (value.name ? String(value.name) + ": " + message : message) : String(value);
                var stack = value.stack ? String(value.stack) : "";
                var rendered = summary && stack.indexOf(summary) === -1 ? summary + "\n" + stack : (stack || summary);
                if (value.cause && typeof value.cause === "object") {
                    var causeParts = ["code", "detail", "url", "status"].filter(function (key) {
                        return value.cause[key] !== undefined;
                    }).map(function (key) {
                        return key + "=" + String(value.cause[key]);
                    });
                    if (causeParts.length > 0) {
                        rendered += "\ncause: " + causeParts.join(", ");
                    }
                }
                return rendered;
            }
            return String(value);
        });
        state.consoleErrors.push(values.join(" "));
        originalConsoleError.apply(console, arguments);
    };

    function addEventTarget(element) {
        var listeners = Object.create(null);
        element.addEventListener = element.addEventListener || function (type, callback) {
            if (typeof callback !== "function") {
                return;
            }
            var callbacks = listeners[String(type)] || (listeners[String(type)] = []);
            if (callbacks.indexOf(callback) === -1) {
                callbacks.push(callback);
            }
        };
        element.removeEventListener = element.removeEventListener || function (type, callback) {
            var callbacks = listeners[String(type)];
            if (!callbacks) {
                return;
            }
            var index = callbacks.indexOf(callback);
            if (index !== -1) {
                callbacks.splice(index, 1);
            }
        };
        element.dispatchEvent = element.dispatchEvent || function (event) {
            var eventObject = typeof event === "string" ? { type: event } : (event || {});
            var type = String(eventObject.type || "");
            eventObject.target = eventObject.target || element;
            eventObject.currentTarget = element;
            var propertyHandler = element["on" + type];
            if (typeof propertyHandler === "function") {
                propertyHandler.call(element, eventObject);
            }
            var callbacks = listeners[type];
            if (callbacks) {
                callbacks.slice().forEach(function (callback) {
                    callback.call(element, eventObject);
                });
            }
            return !eventObject.defaultPrevented;
        };
    }

    function addElementSurface(element, tagName) {
        var upperTagName = String(tagName || "div").toUpperCase();
        element.nodeType = 1;
        element.nodeName = upperTagName;
        element.tagName = upperTagName;
        element.style = element.style || {};
        element.dataset = element.dataset || {};
        element.children = element.children || [];
        element.childNodes = element.children;
        element.attributes = element.attributes || Object.create(null);
        element.parentNode = element.parentNode || null;
        element.ownerDocument = element.ownerDocument || null;
        element.setAttribute = element.setAttribute || function (name, value) {
            var key = String(name);
            var text = String(value);
            element.attributes[key] = text;
            if (key === "id") {
                element.id = text;
                elementsById[text] = element;
            } else if (key.indexOf("data-") === 0) {
                var datasetName = key.slice(5).replace(/-([a-z])/g, function (_, letter) {
                    return letter.toUpperCase();
                });
                element.dataset[datasetName] = text;
            } else {
                element[key] = value;
            }
        };
        element.getAttribute = element.getAttribute || function (name) {
            var key = String(name);
            return Object.prototype.hasOwnProperty.call(element.attributes, key) ? element.attributes[key] : null;
        };
        element.hasAttribute = element.hasAttribute || function (name) {
            return Object.prototype.hasOwnProperty.call(element.attributes, String(name));
        };
        element.appendChild = element.appendChild || function (child) {
            if (child) {
                child.parentNode = element;
                element.children.push(child);
            }
            return child;
        };
        element.removeChild = element.removeChild || function (child) {
            var index = element.children.indexOf(child);
            if (index !== -1) {
                element.children.splice(index, 1);
                child.parentNode = null;
            }
            return child;
        };
        element.remove = element.remove || function () {
            if (element.parentNode && typeof element.parentNode.removeChild === "function") {
                element.parentNode.removeChild(element);
            }
        };
        element.focus = element.focus || function () { };
        element.blur = element.blur || function () { };
        addEventTarget(element);
        return element;
    }

    addEventTarget(globalThis.window);

    function createCanvas(id) {
        var nativeCanvas = (typeof _native !== "undefined" && _native.Canvas) ? new _native.Canvas() : null;
        var nativeGetContext = nativeCanvas && typeof nativeCanvas.getContext === "function" ? nativeCanvas.getContext.bind(nativeCanvas) : null;
        var canvas = {};
        var gpuContext = null;
        var context2d = null;
        var contextType = null;

        Object.defineProperties(canvas, {
            width: {
                enumerable: true,
                get: function () { return nativeCanvas ? nativeCanvas.width : canvas._width; },
                set: function (value) {
                    var width = Math.max(1, Number(value) || 1);
                    canvas._width = width;
                    if (nativeCanvas) {
                        nativeCanvas.width = width;
                    }
                }
            },
            height: {
                enumerable: true,
                get: function () { return nativeCanvas ? nativeCanvas.height : canvas._height; },
                set: function (value) {
                    var height = Math.max(1, Number(value) || 1);
                    canvas._height = height;
                    if (nativeCanvas) {
                        nativeCanvas.height = height;
                    }
                }
            }
        });
        canvas.clientWidth = FRAMEBUFFER_WIDTH;
        canvas.clientHeight = FRAMEBUFFER_HEIGHT;
        addElementSurface(canvas, "canvas");
        canvas.width = FRAMEBUFFER_WIDTH;
        canvas.height = FRAMEBUFFER_HEIGHT;

        canvas.getContext = function (type) {
            var normalizedType = String(type).toLowerCase();
            if (contextType && contextType !== normalizedType) {
                return null;
            }
            if (normalizedType === "webgpu") {
                if (!gpuContext && navigator && navigator.gpu && typeof navigator.gpu._createCanvasContext === "function") {
                    gpuContext = navigator.gpu._createCanvasContext();
                    gpuContext.canvas = canvas;
                }
                if (gpuContext) {
                    contextType = normalizedType;
                }
                return gpuContext;
            }
            if (normalizedType === "2d" && nativeGetContext) {
                context2d = context2d || nativeGetContext("2d");
                if (context2d) {
                    contextType = normalizedType;
                }
                return context2d;
            }
            return null;
        };
        canvas.__babylonLiteReadValidationPixels = function () {
            if (contextType !== "2d" || !context2d || typeof context2d.getImageData !== "function") {
                return null;
            }
            var imageData = context2d.getImageData(0, 0, canvas.width, canvas.height);
            return { pixels: imageData.data, width: imageData.width, height: imageData.height, source: "canvas2d" };
        };
        canvas.toDataURL = function (type, quality) {
            if (!nativeCanvas || typeof nativeCanvas.toDataURL !== "function") {
                throw new TypeError("canvas.toDataURL is not available in this native build.");
            }
            return nativeCanvas.toDataURL(type, quality);
        };
        canvas.getBoundingClientRect = function () {
            return {
                x: 0,
                y: 0,
                top: 0,
                left: 0,
                right: canvas.clientWidth,
                bottom: canvas.clientHeight,
                width: canvas.clientWidth,
                height: canvas.clientHeight
            };
        };

        if (id) {
            canvas.id = id;
            elementsById[id] = canvas;
        }
        return canvas;
    }

    function createElement(tagName) {
        var element;
        if (String(tagName).toLowerCase() === "canvas") {
            element = createCanvas("");
        } else {
            element = addElementSurface({}, tagName);
        }
        element.ownerDocument = documentObject;
        return element;
    }

    var documentObject = {
        createElement: createElement,
        createElementNS: function (_, tagName) {
            return createElement(tagName);
        },
        createTextNode: function (text) {
            return { nodeType: 3, nodeName: "#text", textContent: String(text), parentNode: null };
        },
        getElementById: function (id) {
            var key = String(id);
            if (!elementsById[key]) {
                elementsById[key] = key.toLowerCase().indexOf("canvas") !== -1 ? createCanvas(key) : createElement("div");
                elementsById[key].id = key;
            }
            return elementsById[key];
        },
        querySelector: function (selector) {
            if (String(selector).toLowerCase() === "canvas") {
                return elementsById.renderCanvas;
            }
            if (String(selector).charAt(0) === "#") {
                return documentObject.getElementById(String(selector).slice(1));
            }
            return null;
        },
        querySelectorAll: function (selector) {
            var value = documentObject.querySelector(selector);
            return value ? [value] : [];
        },
        getElementsByTagName: function (tagName) {
            var name = String(tagName).toLowerCase();
            if (name === "html") {
                return [documentObject.documentElement];
            }
            if (name === "head") {
                return [documentObject.head];
            }
            if (name === "body") {
                return [documentObject.body];
            }
            if (name === "canvas") {
                return Object.keys(elementsById).map(function (key) {
                    return elementsById[key];
                }).filter(function (element) {
                    return element && element.tagName === "CANVAS";
                });
            }
            return [];
        },
        addEventListener: function () { },
        removeEventListener: function () { },
        dispatchEvent: function () { return true; }
    };

    documentObject.documentElement = addElementSurface({}, "html");
    documentObject.head = addElementSurface({}, "head");
    documentObject.body = addElementSurface({}, "body");
    documentObject.documentElement.ownerDocument = documentObject;
    documentObject.head.ownerDocument = documentObject;
    documentObject.body.ownerDocument = documentObject;
    elementsById.renderCanvas = createCanvas("renderCanvas");
    elementsById.renderCanvas.ownerDocument = documentObject;
    documentObject.body.appendChild(elementsById.renderCanvas);

    globalThis.__babylonLitePopulateDocument = function (descriptors) {
        (descriptors || []).forEach(function (descriptor) {
            var id = String(descriptor.id || "");
            var element = id === "renderCanvas" ? elementsById.renderCanvas : createElement(descriptor.tagName || "div");
            Object.keys(descriptor.attributes || {}).forEach(function (name) {
                element.setAttribute(name, descriptor.attributes[name]);
            });
            if (id && element.id !== id) {
                element.setAttribute("id", id);
            }
            if (descriptor.textContent !== undefined) {
                element.textContent = String(descriptor.textContent);
            }
            if (descriptor.value !== undefined) {
                element.value = String(descriptor.value);
            }
            if (descriptor.checked !== undefined) {
                element.checked = Boolean(descriptor.checked);
            }
            if (!element.parentNode) {
                documentObject.body.appendChild(element);
            }
        });
    };

    var appendToHead = documentObject.head.appendChild;
    documentObject.head.appendChild = function (element) {
        appendToHead.call(documentObject.head, element);
        if (!element || element.tagName !== "SCRIPT" || !element.src) {
            return element;
        }

        Promise.resolve(globalThis.fetch(element.src))
            .then(function (response) {
                if (!response || !response.ok) {
                    throw new Error("Failed to load script " + element.src + " (status " + (response && response.status) + ").");
                }
                return response.text();
            })
            .then(function (source) {
                documentObject.currentScript = element;
                try {
                    (0, eval)(String(source) + "\n//# sourceURL=" + String(element.src));
                } finally {
                    documentObject.currentScript = null;
                }
                if (typeof element.onload === "function") {
                    element.onload();
                }
            })
            .catch(function (error) {
                console.error(new Error("Failed to load script " + element.src + ": " + String(error)));
                if (typeof element.onerror === "function") {
                    element.onerror(error);
                }
            });
        return element;
    };

    globalThis.document = documentObject;
    globalThis.OffscreenCanvas = globalThis.OffscreenCanvas || function OffscreenCanvas(width, height) {
        var canvas = createCanvas("");
        canvas.width = Number(width) || FRAMEBUFFER_WIDTH;
        canvas.height = Number(height) || FRAMEBUFFER_HEIGHT;
        canvas.clientWidth = canvas.width;
        canvas.clientHeight = canvas.height;
        return canvas;
    };
    globalThis.HTMLCanvasElement = globalThis.HTMLCanvasElement || function HTMLCanvasElement() { };
    globalThis.window.getComputedStyle = globalThis.window.getComputedStyle || function (element) {
        return element && element.style ? element.style : {};
    };

    if (typeof globalThis.__nativeValidationSetFrameTimerEnabled === "function") {
        globalThis.__nativeValidationSetFrameTimerEnabled(true);
    }
    state.frameTimerIntervalId = setInterval(function () {
        if (typeof globalThis.__nativeValidationSetFrameTimerEnabled === "function") {
            globalThis.__nativeValidationSetFrameTimerEnabled(true);
        }
    }, 100);

    if (navigator && navigator.gpu && typeof navigator.gpu.requestAdapter === "function") {
        var originalRequestAdapter = navigator.gpu.requestAdapter;
        navigator.gpu.requestAdapter = function () {
            return Promise.resolve(originalRequestAdapter.apply(this, arguments)).then(function (adapter) {
                if (!adapter || adapter.__babylonLiteValidationWrapped) {
                    return adapter;
                }
                var originalRequestDevice = adapter.requestDevice;
                adapter.requestDevice = function () {
                    return Promise.resolve(originalRequestDevice.apply(this, arguments)).then(function (device) {
                        state.device = device;
                        return device;
                    });
                };
                adapter.__babylonLiteValidationWrapped = true;
                return adapter;
            });
        };
    }

})();
