import UIKit
import MetalKit
import QuartzCore
#if os(tvOS)
import AVKit
#endif

// Validation only: retain several host frames so the asynchronous JS renderer
// contributes a complete scene submission. Never install during timed runs.
private final class PortalCaptureDelegate: BNViewDelegate {
    private let deadline: TimeInterval
    private var remainingFrames = 0
    private var attempted = false

    init?(view: BNView, delay: TimeInterval) {
        deadline = ProcessInfo.processInfo.systemUptime + delay
        super.init(view: view)
    }

    override func draw(in view: MTKView) {
        let manager = MTLCaptureManager.shared()
        if !attempted && ProcessInfo.processInfo.systemUptime >= deadline {
            attempted = true
            guard manager.supportsDestination(.gpuTraceDocument),
                  let device = (view.layer as? CAMetalLayer)?.device else {
                fatalError("Metal frame capture unavailable; enable Metal capture for this diagnostic launch")
            }
            let descriptor = MTLCaptureDescriptor()
            descriptor.captureObject = device
            descriptor.destination = .gpuTraceDocument
            let caches = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0]
            descriptor.outputURL = caches.appendingPathComponent("portal-\(UUID().uuidString).gputrace")
            do {
                try manager.startCapture(with: descriptor)
                remainingFrames = 6
                NSLog("[Playground] capture:started:%@", descriptor.outputURL!.path)
            } catch {
                fatalError("Metal frame capture failed: \(error)")
            }
        }
        super.draw(in: view)
        if remainingFrames > 0 {
            remainingFrames -= 1
            if remainingFrames == 0 {
                manager.stopCapture()
                NSLog("[Playground] capture:completed")
            }
        }
    }
}

class ViewController: UIViewController {

    var mtkView: MTKView!
    var xrView: MTKView!
    var bnView: BNView?

    private var didInitializeView = false
    private var memoryProfileTimer: Timer?
    private var captureDelegate: PortalCaptureDelegate?
    private var previousIdleTimerDisabled: Bool?
    #if os(iOS)
    private var profileBrightness: CGFloat?
    private var previousBrightness: CGFloat?
    #endif
    #if os(tvOS)
    private var displayModeSwitchObserver: NSObjectProtocol?
    private var displayModeSwitchFallback: DispatchWorkItem?
    #endif

    private var isValidationRun: Bool {
        let arguments = CommandLine.arguments
        return arguments.contains("--test")
            || arguments.contains("--test-index")
            || arguments.contains("--save-results")
            || arguments.contains("--once")
            || arguments.contains("--include-excluded")
            || arguments.contains("--hdr10")
    }

    private var isHdr10Run: Bool {
        return CommandLine.arguments.contains("--hdr10")
    }

    private var requestedPreferredFramesPerSecond: Int? {
        let arguments = CommandLine.arguments
        for index in arguments.indices {
            let argument = arguments[index]
            if argument == "--preferred-fps", index + 1 < arguments.count {
                return parsePreferredFramesPerSecond(arguments[index + 1])
            }
            if argument.hasPrefix("--preferred-fps=") {
                return parsePreferredFramesPerSecond(String(argument.dropFirst("--preferred-fps=".count)))
            }
        }
        return nil
    }

    #if !os(tvOS)
    override var supportedInterfaceOrientations: UIInterfaceOrientationMask {
        return isValidationRun ? .landscape : .all
    }

    override var preferredInterfaceOrientationForPresentation: UIInterfaceOrientation {
        return .landscapeRight
    }
    #endif

    override func viewDidLoad() {
        super.viewDidLoad()
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)

        #if os(tvOS)
        if waitForPendingDisplayModeSwitchIfNeeded() {
            return
        }
        #endif

        initializeViewIfNeeded()
        if previousIdleTimerDisabled != nil {
            UIApplication.shared.isIdleTimerDisabled = true
        }
        #if os(iOS)
        if let brightness = profileBrightness {
            view.window?.screen.brightness = brightness
        }
        #endif
    }

    override func viewWillDisappear(_ animated: Bool) {
        super.viewWillDisappear(animated)
        if let disabled = previousIdleTimerDisabled {
            UIApplication.shared.isIdleTimerDisabled = disabled
        }
        #if os(iOS)
        if let brightness = previousBrightness {
            view.window?.screen.brightness = brightness
        }
        #endif
    }

    deinit {
        memoryProfileTimer?.invalidate()
        #if os(tvOS)
        removeDisplayModeSwitchObserver()
        #endif
    }

    private func initializeViewIfNeeded() {
        if didInitializeView {
            return
        }

        guard
            let appDelegate = UIApplication.shared.delegate as? AppDelegate,
            let runtime = appDelegate.runtime
        else { return }
        didInitializeView = true

        #if !os(tvOS)
        if isValidationRun {
            if #available(iOS 16.0, *) {
                setNeedsUpdateOfSupportedInterfaceOrientations()
                view.window?.windowScene?.requestGeometryUpdate(.iOS(interfaceOrientations: .landscapeRight))
            }
        }
        #endif

        setupViews()
        configureFrameRate(mtkView)
        configureDrawable(mtkView)
        configureDrawable(xrView)
        view.layoutIfNeeded()
        logDisplayState("initial BNView attach")

        #if BABYLON_NATIVE_PLAYGROUND_HAS_NATIVEXR
        // Hand the runtime a reference to the XR overlay so NativeXr
        // can render its content into a separate transparent layer
        // when an XR session is active. The runtime keeps the
        // overlay's visibility in sync with the XR session state on
        // its own - the host doesn't need to toggle anything.
        runtime.setXrView(xrView)
        #endif

        // Attach BNView to the main MTKView. Because mtkView's delegate
        // is still nil at this point, BNView auto-installs a managed
        // `BNViewDelegate` that drives the per-frame render callback.
        // Resize is driven separately from `viewDidLayoutSubviews` below.
        // First attach on this runtime triggers GPU device construction +
        // plugin initialization on the JS thread + queued-script flush.
        bnView = BNView(runtime: runtime, view: mtkView)
        if let delayText = ProcessInfo.processInfo.environment["BABYLON_NATIVE_PLAYGROUND_CAPTURE_AFTER_SECONDS"] {
            guard let delay = Double(delayText), delay.isFinite, delay >= 1, let nativeView = bnView else {
                fatalError("BABYLON_NATIVE_PLAYGROUND_CAPTURE_AFTER_SECONDS must be a finite delay of at least one second")
            }
            guard let delegate = PortalCaptureDelegate(view: nativeView, delay: delay) else {
                fatalError("Could not install the Metal capture delegate")
            }
            captureDelegate = delegate
            mtkView.delegate = delegate
        }
        startMemoryProfilingIfRequested()

        #if !os(tvOS)
        // Simple gesture recognizer: forwards touches to BNView.
        let recognizer = UIBabylonGestureRecognizer(
            target: self,
            onTouchDown: { [weak self] (id, x, y) in self?.bnView?.pointerDown(id: Int(id), x: CGFloat(x), y: CGFloat(y)) },
            onTouchMove: { [weak self] (id, x, y) in self?.bnView?.pointerMove(id: Int(id), x: CGFloat(x), y: CGFloat(y)) },
            onTouchUp:   { [weak self] (id, x, y) in self?.bnView?.pointerUp(id: Int(id), x: CGFloat(x), y: CGFloat(y)) }
        )
        mtkView.addGestureRecognizer(recognizer)
        #endif
    }

    private func startMemoryProfilingIfRequested() {
        guard ProcessInfo.processInfo.environment["BABYLON_NATIVE_PLAYGROUND_PROFILE_MEMORY"] == "1" else { return }
        previousIdleTimerDisabled = UIApplication.shared.isIdleTimerDisabled
        UIApplication.shared.isIdleTimerDisabled = true
        NSLog("[Playground] profile:idleTimerDisabled=true")
        #if os(iOS)
        UIDevice.current.isBatteryMonitoringEnabled = true
        if let value = ProcessInfo.processInfo.environment["BABYLON_NATIVE_PLAYGROUND_PROFILE_BRIGHTNESS"] {
            guard let brightness = Double(value), brightness.isFinite, (0...1).contains(brightness) else {
                fatalError("BABYLON_NATIVE_PLAYGROUND_PROFILE_BRIGHTNESS must be between 0 and 1")
            }
            previousBrightness = view.window?.screen.brightness
            profileBrightness = CGFloat(brightness)
            view.window?.screen.brightness = CGFloat(brightness)
        }
        #endif
        let timer = Timer(timeInterval: 2, repeats: true) { [weak self] _ in
            self?.logMemoryProfile()
        }
        memoryProfileTimer = timer
        #if swift(>=4.2)
        RunLoop.main.add(timer, forMode: .common)
        #else
        RunLoop.main.add(timer, forMode: .commonModes)
        #endif
        logMemoryProfile()
    }

    private func logMemoryProfile() {
        var info = task_vm_info_data_t()
        var count = mach_msg_type_number_t(MemoryLayout<task_vm_info_data_t>.size / MemoryLayout<integer_t>.size)
        let result = withUnsafeMutablePointer(to: &info) { pointer in
            pointer.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                task_info(mach_task_self_, task_flavor_t(TASK_VM_INFO), $0, &count)
            }
        }
        guard result == KERN_SUCCESS else {
            NSLog("[Playground] memory:taskInfoError=%d", result)
            return
        }
        let mib = 1024.0 * 1024.0
        // The renderer assigns CAMetalLayer.device directly, not MTKView.device.
        let metalDevice = (xrView.layer as? CAMetalLayer)?.device ?? (mtkView.layer as? CAMetalLayer)?.device
        let metalMiB = metalDevice.map { Double($0.currentAllocatedSize) / mib } ?? Double.nan
        // Unified-memory accounting overlaps; these values must not be added together.
        NSLog("[Playground] memory:uptime=%.3f:footprintMiB=%.2f:residentMiB=%.2f:metalAllocatedMiB=%.2f:thermal=%d",
            ProcessInfo.processInfo.systemUptime,
            Double(info.phys_footprint) / mib,
            Double(info.resident_size) / mib,
            metalMiB,
            ProcessInfo.processInfo.thermalState.rawValue)
        #if os(iOS)
        NSLog("[Playground] power:batteryState=%d:batteryLevel=%.3f:brightness=%.3f:lowPower=%d",
            UIDevice.current.batteryState.rawValue,
            UIDevice.current.batteryLevel,
            view.window?.screen.brightness ?? -1,
            ProcessInfo.processInfo.isLowPowerModeEnabled ? 1 : 0)
        if let expected = profileBrightness, let actual = view.window?.screen.brightness,
           abs(actual - expected) > 0.005 {
            NSLog("[Playground] profileInvalid:brightnessDrift:expected=%.3f:actual=%.3f", expected, actual)
        }
        #endif
        if ProcessInfo.processInfo.environment["BABYLON_NATIVE_WEBGPU_PROFILE_SURFACE"] == "1",
           let layer = mtkView.layer as? CAMetalLayer {
            NSLog("[Playground] surface:bounds=%.0fx%.0f:scale=%.3f:drawable=%.0fx%.0f:viewDrawable=%.0fx%.0f:autoResize=%d",
                mtkView.bounds.width, mtkView.bounds.height, layer.contentsScale,
                layer.drawableSize.width, layer.drawableSize.height,
                mtkView.drawableSize.width, mtkView.drawableSize.height, mtkView.autoResizeDrawable ? 1 : 0)
        }
    }

    #if os(tvOS)
    private func waitForPendingDisplayModeSwitchIfNeeded() -> Bool {
        guard isHdr10Run else {
            return false
        }
        guard #available(tvOS 11.3, *) else {
            return false
        }
        guard let displayManager = view.window?.avDisplayManager, displayManager.isDisplayModeSwitchInProgress else {
            return false
        }

        NSLog("[Playground] Waiting for tvOS display mode switch before initializing renderer.")
        removeDisplayModeSwitchObserver()

        displayModeSwitchObserver = NotificationCenter.default.addObserver(
            forName: NSNotification.Name.AVDisplayManagerModeSwitchEnd,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            guard let self = self else { return }
            self.removeDisplayModeSwitchObserver()
            self.logDisplayState("after tvOS display mode switch")
            self.initializeViewIfNeeded()
        }

        let fallback = DispatchWorkItem { [weak self] in
            guard let self = self else { return }
            self.removeDisplayModeSwitchObserver()
            self.logDisplayState("after tvOS display mode switch timeout")
            self.initializeViewIfNeeded()
        }
        displayModeSwitchFallback = fallback
        DispatchQueue.main.asyncAfter(deadline: .now() + .seconds(10), execute: fallback)
        return true
    }

    private func removeDisplayModeSwitchObserver() {
        if let observer = displayModeSwitchObserver {
            NotificationCenter.default.removeObserver(observer)
            displayModeSwitchObserver = nil
        }
        displayModeSwitchFallback?.cancel()
        displayModeSwitchFallback = nil
    }
    #endif

    private func currentScreen() -> UIScreen? {
        #if os(tvOS)
        return view.window?.windowScene?.screen
        #else
        return view.window?.screen ?? UIScreen.main
        #endif
    }

    private func logDisplayState(_ label: String) {
        #if os(tvOS)
        guard let screen = currentScreen() else {
            NSLog("[Playground] tvOS display %@: no screen", label)
            return
        }

        let modeSize = screen.currentMode?.size ?? .zero
        NSLog("[Playground] tvOS display %@: bounds=%.0fx%.0f nativeBounds=%.0fx%.0f mode=%.0fx%.0f scale=%.3f nativeScale=%.3f maximumFramesPerSecond=%d",
            label,
            screen.bounds.width,
            screen.bounds.height,
            screen.nativeBounds.width,
            screen.nativeBounds.height,
            modeSize.width,
            modeSize.height,
            screen.scale,
            screen.nativeScale,
            screen.maximumFramesPerSecond)
        if #available(tvOS 16.0, *) {
            NSLog("[Playground] tvOS EDR %@: currentHeadroom=%.3f potentialHeadroom=%.3f",
                label, screen.currentEDRHeadroom, screen.potentialEDRHeadroom)
        }
        #endif
    }

    private func parsePreferredFramesPerSecond(_ value: String) -> Int? {
        guard let fps = Int(value), fps > 0 else {
            NSLog("[Playground] Ignoring invalid --preferred-fps value: %@", value)
            return nil
        }
        return fps
    }

    private func configureFrameRate(_ view: MTKView) {
        let requestedFramesPerSecond: Int?
        if let requested = requestedPreferredFramesPerSecond {
            requestedFramesPerSecond = requested
        } else {
            #if os(tvOS)
            requestedFramesPerSecond = 60
            #else
            requestedFramesPerSecond = nil
            #endif
        }

        let maximumFramesPerSecond = currentScreen()?.maximumFramesPerSecond ?? 0
        if let requested = requestedFramesPerSecond {
            view.preferredFramesPerSecond = maximumFramesPerSecond > 0 ? min(requested, maximumFramesPerSecond) : requested
        }
        NSLog("[Playground] MTKView preferredFramesPerSecond=%d requestedFramesPerSecond=%d screenMaximumFramesPerSecond=%d",
            view.preferredFramesPerSecond,
            requestedFramesPerSecond ?? 0,
            maximumFramesPerSecond)
    }

    private func configureDrawable(_ view: MTKView) {
        if isHdr10Run {
            #if os(tvOS)
            view.autoResizeDrawable = false
            #endif
            view.colorPixelFormat = .rgba16Float
            if let layer = view.layer as? CAMetalLayer {
                layer.pixelFormat = view.colorPixelFormat
                // Canvas output is sRGB encoded, including float HDR canvases.
                layer.colorspace = CGColorSpace(name: CGColorSpace.extendedSRGB)
                if #available(tvOS 18.0, iOS 18.0, *) {
                    layer.toneMapMode = .ifSupported
                }
                if #available(tvOS 26.0, iOS 26.0, *) {
                    layer.preferredDynamicRange = .high
                    layer.contentsHeadroom = 10.0
                }
            }
            return
        }

        #if os(tvOS)
        view.autoResizeDrawable = false
        view.colorPixelFormat = .bgra8Unorm_srgb
        if let layer = view.layer as? CAMetalLayer {
            layer.pixelFormat = view.colorPixelFormat
            layer.colorspace = CGColorSpace(name: CGColorSpace.sRGB)
        }
        #else
        view.colorPixelFormat = .bgra8Unorm_srgb
        #endif
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()

        // Babylon Native owns the drawable size (BNView sets
        // autoResizeDrawable = NO), so MTKView no longer reports size
        // changes via its delegate. Drive resize explicitly from layout,
        // passing logical points; BNView applies the device-pixel-ratio
        // internally.
        guard let mtkView = mtkView, let bnView = bnView else { return }
        let size = mtkView.bounds.size
        if size.width > 0 && size.height > 0 {
            bnView.resize(width: UInt(size.width), height: UInt(size.height))
        }
    }

    func setupViews() {
        mtkView = MTKView()
        mtkView.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(mtkView)
        let mtkViews = ["mtkView" : mtkView!]
        view.addConstraints(NSLayoutConstraint.constraints(withVisualFormat: "|[mtkView]|", options: [], metrics: nil, views: mtkViews))
        view.addConstraints(NSLayoutConstraint.constraints(withVisualFormat: "V:|[mtkView]|", options: [], metrics: nil, views: mtkViews))

        xrView = MTKView()
        xrView.translatesAutoresizingMaskIntoConstraints = false
        xrView.isUserInteractionEnabled = false
        xrView.isHidden = true
        view.addSubview(xrView)
        let xrViews = ["xrView" : xrView!]
        view.addConstraints(NSLayoutConstraint.constraints(withVisualFormat: "|[xrView]|", options: [], metrics: nil, views: xrViews))
        view.addConstraints(NSLayoutConstraint.constraints(withVisualFormat: "V:|[xrView]|", options: [], metrics: nil, views: xrViews))
    }
}
