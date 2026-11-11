import { expect } from "chai";

describe("File streaming", function () {
    // Adapted from WebKit's fast/files/blob-stream-crash-2.html.
    it("streams and slices multiple File parts through the Blob API", async function () {
        const file = new File(["a", new Blob(), "b", new Blob(), "c", new Blob(), "d"], "letters.txt");
        const reader = file.stream().getReader();
        const bytes: number[] = [];
        while (true) {
            const result = await reader.read();
            if (result.done) {
                break;
            }
            bytes.push(...Array.from(result.value as Uint8Array));
        }
        expect(bytes).to.deep.equal([97, 98, 99, 100]);
        expect(await file.slice(1, 3).text()).to.equal("bc");
    });
});
