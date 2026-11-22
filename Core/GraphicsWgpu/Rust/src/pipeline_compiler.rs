use std::{io, sync::mpsc, thread};

/// One driver compiler per renderer. Dropping the last sender drains accepted work and exits;
/// a job owns its device inputs, never a reference to the renderer or JS runtime.
pub(crate) struct PipelineCompiler(mpsc::Sender<Box<dyn FnOnce() + Send>>);

impl PipelineCompiler {
    pub(crate) fn new() -> io::Result<Self> {
        let (tx, rx) = mpsc::channel::<Box<dyn FnOnce() + Send>>();
        thread::Builder::new()
            .name("WebGPU compiler".into())
            .spawn(move || {
                for compile in rx {
                    compile();
                }
            })?;
        Ok(Self(tx))
    }

    pub(crate) fn submit(&self, compile: impl FnOnce() + Send + 'static) -> Result<(), String> {
        self.0
            .send(Box::new(compile))
            .map_err(|_| "WebGPU compiler stopped".to_owned())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;

    #[test]
    fn stalled_driver_does_not_block_owner_or_spawn_a_compiler_per_pipeline() {
        let compiler = PipelineCompiler::new().unwrap();
        let (tx, ids) = mpsc::channel();
        let (release, gate) = mpsc::channel();
        let first = tx.clone();
        compiler
            .submit(move || {
                first.send(thread::current().id()).unwrap();
                gate.recv_timeout(Duration::from_secs(5)).unwrap();
            })
            .unwrap();
        let driver = ids.recv_timeout(Duration::from_secs(5)).unwrap();
        assert_ne!(driver, thread::current().id());
        compiler
            .submit(move || tx.send(thread::current().id()).unwrap())
            .unwrap();
        drop(compiler);
        assert!(matches!(ids.try_recv(), Err(mpsc::TryRecvError::Empty)));
        release.send(()).unwrap();
        assert_eq!(ids.recv_timeout(Duration::from_secs(5)).unwrap(), driver);
        assert!(matches!(
            ids.recv_timeout(Duration::from_secs(5)),
            Err(mpsc::RecvTimeoutError::Disconnected)
        ));
    }
}
