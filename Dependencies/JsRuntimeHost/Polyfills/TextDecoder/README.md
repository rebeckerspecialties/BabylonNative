# TextDecoder Polyfill

A C++ implementation of the [WHATWG Encoding API](https://encoding.spec.whatwg.org/) `TextDecoder` interface for use in Babylon Native JavaScript runtimes via [Napi](https://github.com/nodejs/node-addon-api).

## Current State

### Supported

- Decoding `Uint8Array`, `Int8Array`, and other typed array views from UTF-8, UTF-16LE, and UTF-16BE byte sequences.
- Decoding raw `ArrayBuffer` objects.
- Constructing `TextDecoder` with no argument (defaults to `utf-8`).
- UTF-8 and UTF-16 labels defined by the Encoding Standard, including case and ASCII-whitespace normalization.
- The normalized `encoding` property.
- BOM removal and replacement characters for malformed UTF-16 input.
- Calling `decode()` with no argument or `undefined` returns an empty string (matches the Web API).

### Not Supported

- Legacy single-byte and multibyte encodings are not implemented.
- `DataView` is not accepted by `decode()` — due to missing `Napi::DataView` support in the underlying JSI layer.
- Passing a non-BufferSource value (e.g. a string or number) to `decode()` throws a `TypeError`.
- The `fatal` option: decoding errors are not detected and do not throw a `TypeError`.
- The `ignoreBOM` option is not implemented; BOMs are stripped using the default behavior.
- Streaming decode (passing `{ stream: true }` to `decode()`) — each call is stateless.

## Usage

```javascript
const decoder = new TextDecoder();              // utf-8
const utf16Decoder = new TextDecoder("utf-16le");

const bytes = new Uint8Array([72, 101, 108, 108, 111]);
decoder.decode(bytes); // "Hello"

new TextDecoder("utf-16le").decode(new Uint8Array([0x48, 0x00, 0x69, 0x00])); // "Hi"
```

Passing an unsupported encoding throws:

```javascript
new TextDecoder("shift_jis"); // RangeError: TextDecoder: unsupported encoding 'shift_jis'
```
