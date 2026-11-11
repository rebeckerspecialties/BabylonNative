import { expect } from "chai";

describe("TextDecoder UTF-16 and labels", function () {
    it("should decode UTF-16LE, including surrogate pairs", function () {
        const decoder = new TextDecoder("utf-16le");
        const encoded = new Uint8Array([0xff, 0xfe, 0x48, 0x00, 0x69, 0x00, 0x3d, 0xd8, 0x00, 0xde]);
        expect(decoder.encoding).to.equal("utf-16le");
        expect(decoder.decode(encoded)).to.equal("Hi😀");
    });

    it("should decode UTF-16BE", function () {
        const decoder = new TextDecoder("utf-16be");
        const encoded = new Uint8Array([0xfe, 0xff, 0x00, 0x48, 0x00, 0x69]);
        expect(decoder.encoding).to.equal("utf-16be");
        expect(decoder.decode(encoded)).to.equal("Hi");
    });

    it("should normalize UTF-16 aliases", function () {
        for (const label of ["UTF-16", " utf-16le ", "unicode", "ucs-2"]) {
            expect(new TextDecoder(label).encoding).to.equal("utf-16le");
        }
        expect(new TextDecoder("unicodefffe").encoding).to.equal("utf-16be");
    });

    it("should replace malformed UTF-16 input", function () {
        const decoder = new TextDecoder("utf-16le");
        expect(decoder.decode(new Uint8Array([0x00, 0xd8, 0x41, 0x00]))).to.equal("�A");
        expect(decoder.decode(new Uint8Array([0x48, 0x00, 0x69]))).to.equal("H�");
    });

    it("should still throw for a genuinely unsupported encoding", function () {
        expect(() => new TextDecoder("shift_jis")).to.throw(RangeError);
    });
});
