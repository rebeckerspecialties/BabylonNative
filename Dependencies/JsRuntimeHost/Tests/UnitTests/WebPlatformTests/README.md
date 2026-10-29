# Focused Worker conformance regressions

`UPSTREAM_REVISION` records the exact
[web-platform-tests/wpt](https://github.com/web-platform-tests/wpt) commit used
when the broader suite is run outside this repository. The WPT checkout and
`testharness.js` are deliberately not vendored.

`workers/focused-api.js` is a small host-native port of eight assertions that
found real gaps in the initial implementation: worker-global identity and
readonly behavior, EventTarget removal/targeting, `onmessage` normalization and
dispatch, `postMessage()`'s return value, and zero-argument `importScripts()`.
The source links are kept in that file. `workers/support/Worker-structure-message.js`
and `workers/constructors/Worker/terminate.js` are two additional focused WPT
ports for structured transfer and termination.

`runner.js` launches these focused tests directly, without WPT infrastructure,
and adapts document-side structured-clone, transfer, and termination checks to
the JsRuntimeHost unit-test host. It also carries a linked regression for
Servo's throwing-getter structured-clone fix.
