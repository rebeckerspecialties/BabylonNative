(() => {
  const failures = [];
  const cases = [
    "/workers/focused-api.js"
  ];

  let index = 0;
  let timer = 0;

  function fail(name, detail) {
    failures.push(name + ": " + detail);
  }

  function finish() {
    clearTimeout(timer);
    __jsrhWptDone(failures.length === 0, failures.join("\n"));
  }

  function runStructuredMessageCase() {
    try {
      const original = { view: new Uint8Array([23]) };
      original.self = original;
      original.alias = original.view;
      const copy = structuredClone(original);
      copy.view[0] = 31;
      if (copy === original || copy.self !== copy || copy.alias !== copy.view || original.view[0] !== 23)
        fail("structuredClone", "cyclic graph, alias, or buffer independence lost");
      const invalid = structuredClone(new Date(NaN));
      if (!(invalid instanceof Date) || !Number.isNaN(invalid.getTime()))
        fail("structuredClone", "an invalid date became a valid timestamp");
      let visits = 0;
      const snapshot = structuredClone({ get item() { ++visits; return original; } });
      if (visits !== 1 || snapshot.item === original || snapshot.item.self !== snapshot.item)
        fail("structuredClone", "getter must be evaluated once and its value cloned");
      const bytes = structuredClone(original.view, {transfer: [original.view.buffer]});
      if (bytes[0] !== 23 || (original.view.buffer.detached !== true && original.view.buffer.byteLength !== 0))
        fail("structuredClone", "transfer did not preserve receiver bytes and detach the sender");
    } catch (error) { fail("structuredClone", error.stack || String(error)); }
    const name = "/workers/support/Worker-structure-message.js";
    const worker = new Worker(name);
    const input = new ArrayBuffer(20);
    let sawPass = false;

    timer = setTimeout(() => {
      worker.terminate();
      fail(name, "timed out");
      finish();
    }, 10000);

    worker.onerror = event => {
      clearTimeout(timer);
      worker.terminate();
      fail(name, event.message || "worker error");
      finish();
    };

    worker.onmessage = event => {
      if (typeof event.data === "string") {
        sawPass = event.data.indexOf("PASS:") === 0;
        if (!sawPass) fail(name, event.data);
        return;
      }

      clearTimeout(timer);
      const value = event.data;
      if (!sawPass || !value || value.operation !== "find-edges" ||
          !(value.input instanceof ArrayBuffer) || value.input.byteLength !== 20 ||
          value.threshold !== 0.6) {
        fail(name, "structured clone did not preserve the WPT payload");
      }
      worker.terminate();
      runTerminateCase();
    };

    worker.postMessage({ operation: "find-edges", input, threshold: 0.6 }, [input]);
    // Current WebKitGTK exposes the standards-track detached getter before
    // its plain ArrayBuffer byteLength reflection catches up. Either signal
    // confirms the sender backing store is detached; the Node-API adapter
    // independently validates detached state before and after transfer.
    if (input.detached !== true && input.byteLength !== 0) {
      fail(name, "transfer did not detach the sender ArrayBuffer");
    }
  }

  function runThrowingGetterCase() {
    const name = "structured clone preserves a throwing getter exception";
    const worker = new Worker("/workers/support/Worker-structure-message.js");
    const expected = new Error("getter sentinel");
    const transfer = new ArrayBuffer(8);
    let thrown;

    // Servo used to clear the pending JS exception raised while reading an
    // enumerable property and replace it with DataCloneError. Serialization
    // must propagate the original exception and must not detach transferables
    // after serialization has already failed.
    // https://github.com/servo/servo/commit/26f4da824907946569fb673a249a2c9035c1d1e4
    try {
      worker.postMessage({
        get value() {
          throw expected;
        }
      }, [transfer]);
    } catch (error) {
      thrown = error;
    }

    worker.terminate();
    if (thrown !== expected) {
      fail(name, "postMessage replaced or swallowed the getter exception");
    }
    if (transfer.detached === true || transfer.byteLength === 0) {
      fail(name, "postMessage detached a transfer after serialization failed");
    }
    runStructuredMessageCase();
  }
  function runTerminateCase() {
    const name = "/workers/constructors/Worker/terminate.js";
    const worker = new Worker(name);
    let messages = 0;

    worker.onerror = event => {
      clearTimeout(timer);
      worker.terminate();
      fail(name, event.message || "worker error");
      finish();
    };
    worker.onmessage = () => { messages++; };

    timer = setTimeout(() => {
      const expected = messages;
      // Adapt the WPT document harness: hold the parent turn while the Worker
      // queues additional messages, then verify terminate() discards them.
      const start = Date.now();
      while (Date.now() - start < 50) {}
      worker.terminate();

      timer = setTimeout(() => {
        if (messages !== expected) {
          fail(name, "message events queued before terminate() were delivered");
        }
        finish();
      }, 100);
    }, 100);
  }

  function next() {
    clearTimeout(timer);
    if (index === cases.length) {
      runThrowingGetterCase();
      return;
    }

    const name = cases[index++];
    const worker = new Worker(name);
    timer = setTimeout(() => {
      worker.terminate();
      fail(name, "timed out");
      next();
    }, 10000);

    worker.onerror = event => {
      clearTimeout(timer);
      worker.terminate();
      fail(name, event.message || "worker error");
      next();
    };

    worker.onmessage = event => {
      const value = event.data;
      if (!value || value.type !== "focused-results") return;
      clearTimeout(timer);
      for (const test of value.failures || []) {
        fail(name + " / " + test.name, test.message || "failed");
      }
      worker.terminate();
      next();
    };
  }

  next();
})();
