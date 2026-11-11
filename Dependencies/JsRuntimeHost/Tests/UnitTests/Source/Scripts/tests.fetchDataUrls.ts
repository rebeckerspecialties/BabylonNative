import { expect } from "chai";

describe("fetch data URLs", function () {
    this.timeout(30000);

    it("should resolve percent-encoded data URLs locally", async function () {
        const url = "data:text/plain;charset=utf-8,hello%20native%20fetch%21";
        const response = await fetch(url);
        expect(response.ok).to.equal(true);
        expect(response.status).to.equal(200);
        expect(response.url).to.equal(url);
        expect(response.headers.get("content-type")).to.equal("text/plain;charset=utf-8");
        expect(await response.text()).to.equal("hello native fetch!");
    });

    it("should decode base64 data URLs without using the network transport", async function () {
        const response = await fetch("data:application/octet-stream;base64,AAEC/w==");
        const clone = response.clone();
        expect(new Uint8Array(await response.arrayBuffer())).to.eql(new Uint8Array([0, 1, 2, 255]));
        const blob = await clone.blob();
        expect(blob.type).to.equal("application/octet-stream");
        expect(new Uint8Array(await blob.arrayBuffer())).to.eql(new Uint8Array([0, 1, 2, 255]));
    });

    // Adapted from WPT fetch/data-urls/processing.any.js and resources/data-urls.json.
    const dataUrlCases: Array<[string, string, number[]]> = [
        ["data:,", "text/plain;charset=US-ASCII", []],
        ["data:,%FF", "text/plain;charset=US-ASCII", [255]],
        ["data:text/plain,X", "text/plain", [88]],
        ["data:,X#fragment", "text/plain;charset=US-ASCII", [88]],
        ["data:;BASe64,WA", "text/plain;charset=US-ASCII", [88]],
        ["data:  ;charset=x   ;  base64,W%20A", "text/plain;charset=x", [88]]
    ];

    for (const [url, expectedType, expectedBody] of dataUrlCases) {
        it(`should process WPT data URL case ${JSON.stringify(url)}`, async function () {
            const response = await fetch(url);
            expect(response.headers.get("content-type")).to.equal(expectedType);
            expect(Array.from(new Uint8Array(await response.arrayBuffer()))).to.eql(expectedBody);
        });
    }

    // Adapted from WPT's forgiving-base64 vectors and Chromium's DataURL tests.
    const base64Cases: Array<[string, number[]]> = [
        ["abcd", [105, 183, 29]],
        ["ab%09%0A%0C%0D%20cd", [105, 183, 29]],
        ["ab==", [105]],
        ["/A", [252]],
        ["YR", [97]]
    ];

    for (const [encoded, expectedBody] of base64Cases) {
        it(`should forgiving-base64 decode ${JSON.stringify(encoded)}`, async function () {
            const response = await fetch(`data:application/octet-stream;base64,${encoded}`);
            expect(Array.from(new Uint8Array(await response.arrayBuffer()))).to.eql(expectedBody);
        });
    }

    for (const encoded of ["a", "ab===", "ab%0Bcd", "=a", "a=b"]) {
        it(`should reject invalid WPT base64 case ${JSON.stringify(encoded)}`, async function () {
            let error: unknown;
            try {
                await fetch(`data:application/octet-stream;base64,${encoded}`);
            } catch (caught) {
                error = caught;
            }
            expect(error).to.be.instanceOf(TypeError);
        });
    }
});
