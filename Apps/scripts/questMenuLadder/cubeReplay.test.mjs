import { test } from "node:test";
import assert from "node:assert/strict";
import { cubeCheckpoint, instrument } from "./buildCubeReplay.mjs";

test("unknown playback bundles are not partially patched", () => {
    assert.throws(() => instrument(""), /Unrecognized/);
});

test("each control changes only the default cube and the final baseline is restored", t => {
    const selection = Object.getOwnPropertyDescriptor(globalThis,"__questPlaybackDate");
    t.after(() => { if (selection) Object.defineProperty(globalThis,"__questPlaybackDate",selection); else delete globalThis.__questPlaybackDate; });
    let clock = 0, render, found = 0;
    t.mock.method(performance,"now",()=>clock);
    t.mock.method(console,"info",()=>{});
    const plugin = {_frame:0,isEnabled:true};
    const mesh = {layerMask:123,thinInstanceCount:2,isEnabled:()=>true,edgesRenderer:{isEnabled:true},
        _userThinInstanceBuffersStorage:{data:{animationFrame:new Float32Array([0,19,0])}},
        material:{disableLighting:false,pluginManager:{getPlugin:()=>plugin}}};
    const tick = cubeCheckpoint({getMeshByName:name=>{ found++; assert.equal(name,"baseMesh-default"); return mesh; },
        getActiveIndices:()=>42,onBeforeRenderObservable:{add:fn=>render=fn,remove:fn=>{ assert.equal(fn,render);render=null; }}});
    globalThis.__questPlaybackDate = "2016-01-09";
    tick(); assert.equal(found,0);
    globalThis.__questPlaybackDate = "2016-01-10";
    for (let stage=0;stage<=8;stage++) {
        clock = stage*25000;
        tick();
        assert.equal(mesh.edgesRenderer.isEnabled,[0,7,8].includes(stage));
        assert.equal(mesh.material.disableLighting,stage===4);
        assert.equal(mesh.layerMask,stage===5?0:123);
        assert.equal(plugin.__questCubeFixedFrame,stage===2?0:undefined);
        render?.();
    }
    assert.equal(render,null);
    const end = console.info.mock.calls.find(call=>call.arguments[0]==="Quest cube end");
    assert.equal(JSON.parse(end.arguments[1]).animated,1);
});
