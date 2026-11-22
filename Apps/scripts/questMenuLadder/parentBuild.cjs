// Apply exactly the maintained scene policy to the pinned, already-bundled device fixtures.
const { readFileSync } = require("node:fs");
const path = require("node:path");
const patch = readFileSync(path.join(__dirname, "../../..", "Patches/BabylonJS/0010-no-mirror-parent-clear.patch"), "utf8");
const method = patch.split(/\r?\n/).filter((s) => s.startsWith("+")).map((s) => s.slice(1)).join("\n")
    .match(/private _canSkipParentClear\(camera: Nullable<Camera>\): boolean (\{[\s\S]*?\n    \})/)[1];
function once(source, before, after) {
    if (source.split(before).length !== 2) throw Error("Expected exactly one occurrence: " + before);
    return source.replace(before, () => after);
}
function withParentClear(bundle) {
    const rigMode = bundle.match(/\.cameraRigMode!==([\w$]+)\.RIG_MODE_CUSTOM/);
    // Some bundles inline the enum. Read that value from the equivalent restore-backbuffer guard.
    const constant = rigMode?.[1] ? rigMode[1] + ".RIG_MODE_CUSTOM" :
        bundle.match(/(\d+)!==this\._activeCamera\.cameraRigMode&&!this\.prePass/)?.[1];
    if (!constant) throw Error("Unrecognized rig mode");
    const body = method.replace("Constants.RIG_MODE_CUSTOM", constant);
    return once(once(bundle, "}_clearFrameBuffer(e){", "}_canSkipParentClear(camera)" + body + "_clearFrameBuffer(e){"),
        "this._clearFrameBuffer(this.activeCamera);", "if(!this._canSkipParentClear(this.activeCamera))this._clearFrameBuffer(this.activeCamera);");
}
module.exports = { once, withParentClear, method };
