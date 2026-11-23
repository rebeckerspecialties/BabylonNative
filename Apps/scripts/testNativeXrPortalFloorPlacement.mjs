import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import test from "node:test";
import vm from "node:vm";

const require = createRequire(new URL("../package.json", import.meta.url));
const BABYLON = require("babylonjs");
const source = readFileSync(new URL("../Playground/Scripts/native_xr_babylon_portal_demo.js", import.meta.url), "utf8");
const context = vm.createContext({ BABYLON });
vm.runInContext(source, context);
const findFloor = context.findNativeArPortalFloorForPosition;
const point = (x, y, z) => new BABYLON.Vector3(x, y, z);
const plane = (points = [[-1, -1], [1, -1], [1, 1], [-1, 1]], transform = BABYLON.Matrix.Identity()) => ({
    polygonDefinition: points.map(([x, z]) => point(x, 0, z)),
    transformationMatrix: transform,
});

test("zero floors and incomplete polygons cannot place", () => {
    assert.equal(findFloor(point(0, 0, 0), {}), null);
    assert.equal(findFloor(point(0, 0, 0), { floor: plane([]) }), null);
    assert.equal(findFloor(point(0, 0, 0), { floor: plane([[0, 0], [1, 1]]) }), null);
    assert.equal(findFloor(point(0, 0, 0), { floor: {} }), null);
});

test("one floor accepts its interior and exact boundary, including id zero", () => {
    for (const p of [point(0, 0, 0), point(1, 0, 0), point(-1, 0, -1)]) {
        assert.equal(findFloor(p, { 0: plane() })?.id, "0");
    }
});

test("rejects the observed infinite-floor extension instead of expanding by 1.5 metres", () => {
    assert.equal(findFloor(point(2.612, 0, 0), { floor: plane() }), null);
    assert.equal(findFloor(point(-2.612, 0, 0), { floor: plane() }), null);
});

test("allows small edge noise but rejects points beyond it on either side", () => {
    for (const sign of [-1, 1]) {
        assert.equal(findFloor(point(sign * 1.04, 0, 0), { floor: plane() })?.id, "floor");
        assert.equal(findFloor(point(sign * 1.06, 0, 0), { floor: plane() }), null);
    }
    assert.equal(findFloor(point(1.04, 0, 1.04), { floor: plane() }), null);
});

test("uses the polygon rather than its bounding box, in both winding orders", () => {
    const points = [[0, -1], [1, 0], [0, 1], [-1, 0]];
    for (const polygon of [points, points.toReversed()]) {
        assert.equal(findFloor(point(0.8, 0, 0.8), { floor: plane(polygon) }), null);
        assert.equal(findFloor(point(0.2, 0, 0.2), { floor: plane(polygon) })?.id, "floor");
    }
});

test("transforms local plane geometry into the same world space as the hit", () => {
    const transform = BABYLON.Matrix.RotationY(Math.PI / 4).multiply(BABYLON.Matrix.Translation(4, -1.2, -3));
    const floor = plane(undefined, transform);
    assert.equal(findFloor(point(4, -1.2, -3), { floor })?.id, "floor");
    assert.equal(findFloor(point(4, 0, -3), { floor }), null);
    assert.equal(findFloor(point(5.3, -1.2, -1.7), { floor }), null);
});

test("rejects concave notches and degenerate polygons", () => {
    const concave = plane([[-1, -1], [1, -1], [1, 1], [0, 0], [-1, 1]]);
    assert.equal(findFloor(point(0, 0, 0.8), { floor: concave }), null);
    assert.equal(findFloor(point(0, 0, -0.8), { floor: concave })?.id, "floor");
    assert.equal(findFloor(point(0, 0, 0), { floor: plane([[0, 0], [0, 0], [0, 0]]) }), null);
});

test("chooses the closest-height valid floor among several and ignores inherited entries", () => {
    const floors = Object.assign(Object.create({ inherited: plane() }), {
        low: plane(undefined, BABYLON.Matrix.Translation(0, -0.1, 0)),
        nearest: plane(undefined, BABYLON.Matrix.Translation(0, 0.02, 0)),
        distant: plane(undefined, BABYLON.Matrix.Translation(10, 0, 0)),
    });
    assert.equal(findFloor(point(0, 0, 0), floors)?.id, "nearest");
    assert.equal(findFloor(point(0, 0.2, 0), { floor: plane() }), null);
    assert.equal(findFloor(point(0, -0.2, 0), { floor: plane() }), null);
});

test("non-finite hits and plane vertices cannot create a placement", () => {
    for (const p of [null, point(NaN, 0, 0), point(0, Infinity, 0), point(0, 0, NaN)]) {
        assert.equal(findFloor(p, { floor: plane() }), null);
    }
    assert.equal(findFloor(point(0, 0, 0), { floor: plane([[0, 0], [NaN, 1], [1, 0]]) }), null);
});
