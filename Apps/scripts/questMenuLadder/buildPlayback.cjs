const { readFileSync, writeFileSync } = require("node:fs");
const { once, withParentClear } = require("./parentBuild.cjs");
const [input, output] = process.argv.slice(2);
let source = withParentClear(readFileSync(input, "utf8"));
source = once(source, "const i=e.rigParent?.rigCameras[0]?.outputRenderTarget?.getInternalTexture()?._hardwareTexture?.underlyingResource,n=t.getInternalTexture()?._hardwareTexture?.underlyingResource;if(!i||!n||i===n)return",
    "if(!(" + require("./eyeClear.cjs").needsSeparateEyeClear + ")(e.rigParent?.rigCameras[0]?.outputRenderTarget,t))return");
source = once(source, "const g=h.baseExperience.camera;g.compensateOnFirstFrame",
    "const g=h.baseExperience.camera;g.skipUnusedParentFramebufferClear=true;g.compensateOnFirstFrame");
if (!source.includes("n&&a.setDepthStencilTexture(n),r.disableRescaling()")) {
    source = once(source, "n&&(a._depthStencilTexture=n),r.disableRescaling()", "n&&a.setDepthStencilTexture(n),r.disableRescaling()");
}
writeFileSync(output, source);
