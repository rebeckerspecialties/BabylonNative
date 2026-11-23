import {readFileSync,writeFileSync} from "node:fs";
import {eyeDemand,ringSegments,ringIndices} from "./stereoRingLod.mjs";
import {ringReplay} from "./stereoRingReplay.mjs";

const [input,output] = process.argv.slice(2);
if(!input||!output)throw Error("Expected an input checkpoint fixture and output JS path");
const source=readFileSync(input,"utf8");
const site="const timer = setInterval(() => {\n        const stats";
if(source.split(site).length!==2)throw Error("Unrecognized checkpoint fixture");
function checkpoint(scene){
    const modes=["baseline","cull","lod","cull","baseline"];
    let start,stage=-1,replay;
    return ()=>{
        if(new Date(globalThis.__questPlaybackDate||0).toISOString().slice(0,10)!=="2016-01-10"||stage===modes.length)return;
        if(!replay) {replay=ringReplay(scene);start=performance.now();}
        const next=Math.min(modes.length,Math.floor((performance.now()-start)/25000));
        if(stage===next)return;
        if(stage>=0)console.info("Quest ring end",JSON.stringify({...replay.stats(),stage,indices:scene.getActiveIndices()}));
        stage=next;
        if(next===modes.length){replay.dispose();console.info("Quest area complete");}
        else {replay.setMode(modes[next]);console.info("Quest ring begin",next,modes[next]);}
    };
}
const functions=[eyeDemand,ringSegments,ringIndices,ringReplay,checkpoint].join("\n");
writeFileSync(output,source.replace(site,()=>functions+"\nconst ringTick=checkpoint(scene);const timer = setInterval(() => {\n        ringTick();const stats"));
