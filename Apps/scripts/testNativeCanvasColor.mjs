import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";

const rust = readFileSync(new URL("../../Core/GraphicsWgpu/Rust/src/lib.rs", import.meta.url), "utf8");
test("float canvas presentation explicitly selects the WebGPU sRGB transfer", () => {
    const body = rust.slice(rust.indexOf("pub fn configure_local_surface("), rust.indexOf("pub fn reconfigure_local_surface("));
    assert.match(body, /let encoded_hdr_supported = caps\s*\.color_spaces\(wgpu::TextureFormat::Rgba16Float\)\s*\.contains\(wgpu::SurfaceColorSpaces::EXTENDED_SRGB\);/);
    assert.match(body, /if hdr10 \{\s*config\.format = wgpu::TextureFormat::Rgba16Float;[^}]*config\.color_space = wgpu::SurfaceColorSpace::ExtendedSrgb;/);
    assert.match(body, /if hdr10 && !encoded_hdr_supported \{\s*return Err\("HDR canvas presentation requires Rgba16Float with the extended sRGB transfer;[^\n]*Disable HDR for SDR presentation\./);
    assert.match(body, /else if caps\.formats\.contains\(&wgpu::TextureFormat::Bgra8UnormSrgb\)/);
    const swift = readFileSync(new URL("../Playground/iOS/ViewController.swift", import.meta.url), "utf8");
    assert.match(swift, /CGColorSpace\(name: CGColorSpace\.extendedSRGB\)/);
    assert.doesNotMatch(swift, /extendedLinearITUR_2020|extendedLinearDisplayP3/);
});

test("canvas capture does not apply a second sRGB encoding to float output", () => {
    const capture = rust.slice(rust.indexOf("pub fn render(\n"), rust.indexOf("static SKIP_UNCHANGED"));
    assert.match(capture, /map_readback_buffer_to_rgba\([\s\S]*source_texture\.format\(\),\s*false,/);
    assert.match(rust, /let encode_rgb = if float_rgb_is_linear \{ linear_to_srgb_u8 \} else \{ linear_to_unorm_u8 \};/);
});

test("readback encoders distinguish encoded midtones from linear light", () => {
    const directory = mkdtempSync(join(tmpdir(), "canvas-color-"));
    try {
        const functions = ["linear_to_unorm_u8", "linear_to_srgb_u8"].map(name => {
            const match = rust.match(new RegExp(`    fn ${name}\\(value: f32\\) -> u8 \\{[\\s\\S]*?\\n    \\}`));
            assert.ok(match, name);
            return match[0];
        }).join("\n");
        const file = join(directory, "color.rs");
        const executable = join(directory, "color");
        writeFileSync(file, `${functions}\nfn main() {
            assert_eq!(linear_to_unorm_u8(0.5), 128);
            assert_eq!(linear_to_srgb_u8(0.5), 188);
            for (input, expected) in [(0.0, 0), (1.0, 255), (-1.0, 0), (2.0, 255), (f32::NAN, 0)] {
                assert_eq!(linear_to_unorm_u8(input), expected);
                assert_eq!(linear_to_srgb_u8(input), expected);
            }
        }`);
        const compile = spawnSync(process.env.RUSTC || "rustc", [file, "-o", executable], { encoding: "utf8", timeout: 30000 });
        assert.equal(compile.status, 0, compile.stderr || String(compile.error));
        const result = spawnSync(executable, [], { encoding: "utf8", timeout: 5000 });
        assert.equal(result.status, 0, result.stderr || String(result.error));
    } finally { rmSync(directory, { recursive: true, force: true }); }
});
