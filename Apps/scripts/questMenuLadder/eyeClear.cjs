// Side-by-side stereo shares one subresource. Array stereo shares a texture, not a subresource.
function needsSeparateEyeClear(left, right) {
    if (!left || !right || left === right) return false;
    const texture = (target) => target.getInternalTexture()?._hardwareTexture?.underlyingResource;
    const l = texture(left), r = texture(right);
    return !!l && !!r && (l !== r || (left.layerIndex ?? 0) !== (right.layerIndex ?? 0));
}

module.exports = { needsSeparateEyeClear };
