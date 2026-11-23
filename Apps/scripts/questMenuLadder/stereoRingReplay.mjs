import {eyeDemand,ringSegments,ringIndices} from "./stereoRingLod.mjs";

// Paused-checkpoint benchmark only. Never replace or reorder simulation-owned instance buffers.
export function ringReplay(scene) {
    const families = [];
    for (const [name, max, base] of [["skillRing-avatarInstancer",32,0.525],["visualizedDataContainerRing",64,0.55]]) {
        const src = scene.getMeshByName(name);
        if (!src?.thinInstanceCount || src.getTotalVertices() !== (max+1)**2) throw Error("Unexpected ring geometry: "+name);
        const storage = src._userThinInstanceBuffersStorage;
        const count = src.thinInstanceCount;
        const streams = [["matrix",16,src._thinInstanceDataStorage.matrixData]];
        for (const [key,data] of Object.entries(storage?.data ?? {})) streams.push([key,storage.strides[key],data]);
        if (streams.some(([,stride,data]) => !data || data.length < count*stride)) throw Error("Missing source attributes: "+name);
        const groups=[];
        for (let n=8;n<=max;n*=2) {
            const mesh=src.clone(name+"-xr-"+n,src.parent,true);
            mesh.setEnabled(false);
            mesh.makeGeometryUnique();
            if(n!==max) mesh.setIndices(ringIndices(max,n));
            mesh.isPickable=false;
            mesh.alwaysSelectAsActiveMesh=true;
            mesh.doNotSyncBoundingInfo=true;
            const attrs=streams.map(([key,stride,data])=>{
                const output=new Float32Array(count*stride);
                mesh.thinInstanceSetBuffer(key,output,stride,false);
                return {key,stride,data,output};
            });
            groups.push({mesh,attrs,members:new Int32Array(count),size:0});
        }
        families.push({src,max,base,count,groups,streams,previous:new Int16Array(count).fill(-1),
            vp:[src.getWorldMatrix().clone(),src.getWorldMatrix().clone()],
            mask:src.layerMask,radius:storage?.data?.radius,previousRadius:storage?.data?.previousRadius});
    }
    let mode="baseline", uploads=0, lastCounts=[];
    const render=()=>{
        if(mode==="baseline")return;
        const eyes=scene.activeCamera?.rigCameras;
        if(eyes?.length!==2)throw Error("Ring qualification requires two cameras");
        for(const f of families) {
            const world=f.src.computeWorldMatrix();
            for(let e=0;e<2;++e) world.multiplyToRef(eyes[e].getTransformationMatrix(),f.vp[e]);
            const data=f.streams[0][2];
            for(const g of f.groups)g.size=0;
            let dirty=false;
            for(let i=0;i<f.count;i++){
                const j=i*16,x=data[j+12],y=data[j+13],z=data[j+14];
                // sqrt(||A||1 * ||A||inf) bounds maximum stretch even for affine shear.
                const a=Math.abs(data[j]),b=Math.abs(data[j+1]),c=Math.abs(data[j+2]),
                    d=Math.abs(data[j+4]),e=Math.abs(data[j+5]),g=Math.abs(data[j+6]),
                    h=Math.abs(data[j+8]),k=Math.abs(data[j+9]),l=Math.abs(data[j+10]);
                const scale=Math.sqrt(Math.max(a+b+c,d+e+g,h+k+l)*Math.max(a+d+h,b+e+k,c+g+l));
                const r=scale*(f.base+Math.max(Math.abs(f.radius?.[i]??0),Math.abs(f.previousRadius?.[i]??0)));
                let px=-1;
                for(let eye=0;eye<2;eye++){
                    const target=eyes[eye].outputRenderTarget;
                    px=Math.max(px,eyeDemand(f.vp[eye].m,x,y,z,r,target.getRenderWidth(),target.getRenderHeight()));
                }
                const n=mode==="cull" ? (px<0?0:f.max) : ringSegments(px,f.max,f.previous[i]);
                if(f.previous[i]!==n)dirty=true;
                f.previous[i]=n;
                if(n){const group=f.groups[Math.log2(n)-3];group.members[group.size++]=i;}
            }
            if(!dirty)continue;
            for(const group of f.groups){
                group.mesh.setEnabled(group.size!==0);
                if(group.size===0)continue;
                for(const {key,stride,data,output} of group.attrs){
                    for(let k=0;k<group.size;k++){
                        const source=group.members[k]*stride;
                        for(let t=0;t<stride;t++)output[k*stride+t]=data[source+t];
                    }
                    group.mesh.thinInstanceBufferUpdated(key);
                    uploads++;
                }
                group.mesh.thinInstanceCount=group.size;
            }
        }
        lastCounts=families.map(f=>f.groups.map(g=>g.size));
    };
    const observer=scene.onBeforeRenderObservable.add(render);
    return {
        setMode(next){
            mode=next;
            for(const f of families){
                f.src.layerMask=next==="baseline"?f.mask:0;
                f.previous.fill(-1);
                for(const g of f.groups)g.mesh.setEnabled(false);
            }
            uploads=0;lastCounts=[];
        },
        stats(){return {mode,uploads,counts:lastCounts};},
        dispose(){
            scene.onBeforeRenderObservable.remove(observer);
            for(const f of families){f.src.layerMask=f.mask;for(const g of f.groups)g.mesh.dispose();}
        }
    };
}
