import { resolve } from "node:path";
import { repoRoot, verifyBundle } from "./babylonBundleContract.mjs";

try {
    let directory;
    if (process.argv[2] === "--xcode-resources") {
        const { TARGET_BUILD_DIR, UNLOCALIZED_RESOURCES_FOLDER_PATH } = process.env;
        if (!TARGET_BUILD_DIR || !UNLOCALIZED_RESOURCES_FOLDER_PATH) throw new Error("Missing Xcode product resource directory.");
        directory = resolve(TARGET_BUILD_DIR, UNLOCALIZED_RESOURCES_FOLDER_PATH, "Scripts");
    } else {
        directory = resolve(process.argv[2] || `${repoRoot}/Apps/node_modules/babylonjs`);
    }
    const receipt = verifyBundle(directory, process.argv[3] || "optimized", process.argv[4] === "--packaged");
    console.log(`Verified Babylon.js ${receipt.variant} bundle: ${receipt.files["babylon.max.js"]}`);
} catch (error) {
    console.error(`Babylon bundle validation failed: ${error.message}\nRun node Apps/scripts/buildBabylonBundle.mjs with the pinned source and installed build dependencies.`);
    process.exitCode = 1;
}
