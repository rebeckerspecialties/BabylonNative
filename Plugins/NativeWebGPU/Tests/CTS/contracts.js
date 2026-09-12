// Binding diagnostics only. These are not CTS tests and do not replace CTS assertions.
(async () => {
    const adapter = await navigator.gpu.requestAdapter();
    const device = await adapter.requestDevice();
    const checks = [];
    function record(name, pass, actual) {
        const check = { type: 'contract', name, pass, actual };
        checks.push(check);
        __ctsLog(JSON.stringify(check));
    }
    record('GPUDevice identity', typeof GPUDevice === 'function' && device instanceof GPUDevice,
        typeof GPUDevice);
    device.pushErrorScope('validation');
    const emptyScope = await device.popErrorScope();
    record('empty error scope resolves null', emptyScope === null,
        emptyScope === undefined ? 'undefined' : emptyScope);
    let extraPop;
    try {
        extraPop = { resolved: String(await device.popErrorScope()) };
    } catch (error) {
        extraPop = { rejected: error.name };
    }
    record('unbalanced pop rejects OperationError', extraPop.rejected === 'OperationError', extraPop);
    device.destroy();
    let timer;
    const lost = await Promise.race([
        device.lost.then(info => ({ reason: info.reason })),
        new Promise(resolve => { timer = setTimeout(() => resolve('pending after 1000ms'), 1000); }),
    ]);
    clearTimeout(timer);
    record('destroy resolves device.lost', lost.reason === 'destroyed', lost);
    __ctsDone(checks.every(check => check.pass));
})().catch(error => {
    __ctsLog(JSON.stringify({ type: 'fatal', message: String(error), stack: error.stack }));
    __ctsDone(false);
});
