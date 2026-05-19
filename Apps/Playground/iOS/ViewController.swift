import UIKit
import MetalKit
import QuartzCore
#if os(tvOS)
import AVKit
#endif

class ViewController: UIViewController {

    var mtkView: MTKView!
    var xrView: MTKView!
    var bnView: BNView?

    private var didInitializeView = false
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
    }

    deinit {
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

        // Hand the runtime a reference to the XR overlay so NativeXr
        // can render its content into a separate transparent layer
        // when an XR session is active. The runtime keeps the
        // overlay's visibility in sync with the XR session state on
        // its own - the host doesn't need to toggle anything.
        runtime.setXrView(xrView)

        // Attach BNView to the main MTKView. Because mtkView's delegate
        // is still nil at this point, BNView auto-installs a managed
        // `BNViewDelegate` that drives the per-frame render callback.
        // Resize is driven separately from `viewDidLayoutSubviews` below.
        // First attach on this runtime triggers GPU device construction +
        // plugin initialization on the JS thread + queued-script flush.
        bnView = BNView(runtime: runtime, view: mtkView)

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
                layer.colorspace = CGColorSpace(name: CGColorSpace.extendedLinearITUR_2020)
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
            layer.colorspace = CGColorSpace(name: CGColorSpace.extendedLinearDisplayP3)
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
