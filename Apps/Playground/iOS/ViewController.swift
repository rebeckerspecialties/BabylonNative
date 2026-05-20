import UIKit
import MetalKit
import QuartzCore
#if os(tvOS)
import AVKit
#endif

class ViewController: UIViewController {

    var mtkView: MTKView!
    var xrView: MTKView!

    private let comparisonWidth = 600
    private let comparisonHeight = 400
    private let maxDrawableDimension: CGFloat = 16384
    private var didInitializeBridge = false
    private var lastXrActive = false
    private var isApplyingDrawableSize = false
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

        initializeBridgeIfNeeded()
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()

        if didInitializeBridge, mtkView != nil, xrView != nil, mtkView.delegate != nil {
            applyDrawableSize(initialDrawableSize(), notifyBridge: true)
        }
    }

    deinit {
        #if os(tvOS)
        removeDisplayModeSwitchObserver()
        #endif
    }

    private func initializeBridgeIfNeeded() {
        if didInitializeBridge {
            return
        }

        guard
            let appDelegate = UIApplication.shared.delegate as? AppDelegate,
            let bridge = appDelegate._bridge
        else { return }
        didInitializeBridge = true

        #if !os(tvOS)
        if isValidationRun {
            if #available(iOS 16.0, *) {
                setNeedsUpdateOfSupportedInterfaceOrientations()
                view.window?.windowScene?.requestGeometryUpdate(.iOS(interfaceOrientations: .landscapeRight))
            }
        }
        #endif
        
        setupViews()
        
        let device = MTLCreateSystemDefaultDevice()
        mtkView.device = device
        xrView.device = device
        
        configureFrameRate(mtkView)
        configureFrameRate(xrView)
        configureDrawable(mtkView)
        configureDrawable(xrView)
        mtkView.depthStencilPixelFormat = .depth32Float
        
        #if !os(tvOS)
        // Simple gesture recognizer, just provides platform to handle input events
        let gesture = UIBabylonGestureRecognizer(
            target: self,
            onTouchDown: bridge.setTouchDown,
            onTouchMove: bridge.setTouchMove,
            onTouchUp: bridge.setTouchUp
        )
        mtkView.addGestureRecognizer(gesture)
        #endif
        
        view.layoutIfNeeded()
        let initialSize = initialDrawableSize()
        applyDrawableSize(initialSize, notifyBridge: false)
        let initialWidth = drawableDimension(initialSize.width)
        let initialHeight = drawableDimension(initialSize.height)
        logDisplayState("initial drawable")
        
        bridge.initialize(
            mtkView,
            screenScale:Float(screenScale()),
            width:initialWidth,
            height:initialHeight,
            comparisonWidth:Int32(comparisonWidth),
            comparisonHeight:Int32(comparisonHeight),
            xrView:Unmanaged.passUnretained(xrView).toOpaque()
        )
        mtkView.delegate = self
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
            self.initializeBridgeIfNeeded()
        }

        let fallback = DispatchWorkItem { [weak self] in
            guard let self = self else { return }
            self.removeDisplayModeSwitchObserver()
            self.logDisplayState("after tvOS display mode switch timeout")
            self.initializeBridgeIfNeeded()
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

    private func initialDrawableSize() -> CGSize {
        #if os(tvOS)
        let nativeBounds = currentScreen()?.nativeBounds ?? .zero
        if isValidDrawableSize(nativeBounds.size) {
            return nativeBounds.size
        }
        let scale = nativeScale()
        return validDrawableSize(from: [
            scaledSize(view.bounds.size, scale: scale),
            CGSize(width: 480, height: 320),
        ])
        #else
        let screen = currentScreen() ?? UIScreen.main
        let scale = screenScale()
        return validDrawableSize(from: [
            scaledSize(view.bounds.size, scale: scale),
            scaledSize(view.window?.bounds.size ?? .zero, scale: scale),
            scaledSize(screen.bounds.size, scale: scale),
            screen.nativeBounds.size,
            CGSize(width: 480, height: 320),
        ])
        #endif
    }

    private func currentScreen() -> UIScreen? {
        #if os(tvOS)
        return view.window?.windowScene?.screen
        #else
        return view.window?.screen ?? UIScreen.main
        #endif
    }

    private func nativeScale() -> CGFloat {
        if let screen = currentScreen() {
            if isValidScale(screen.nativeScale) {
                return screen.nativeScale
            }
            if isValidScale(screen.scale) {
                return screen.scale
            }
        }
        return isValidScale(view.contentScaleFactor) ? view.contentScaleFactor : 1
    }

    private func screenScale() -> CGFloat {
        if let scale = currentScreen()?.scale, isValidScale(scale) {
            return scale
        }
        return nativeScale()
    }

    private func drawableDimension(_ value: CGFloat) -> Int32 {
        guard value.isFinite && value > 0 else {
            return 1
        }

        return Int32(min(value.rounded(), maxDrawableDimension))
    }

    private func isValidScale(_ value: CGFloat) -> Bool {
        return value.isFinite && value > 0 && value <= 8
    }

    private func isValidDrawableSize(_ size: CGSize) -> Bool {
        return size.width.isFinite
            && size.height.isFinite
            && size.width > 0
            && size.height > 0
            && size.width <= maxDrawableDimension
            && size.height <= maxDrawableDimension
    }

    private func scaledSize(_ size: CGSize, scale: CGFloat) -> CGSize {
        guard isValidScale(scale) else {
            return .zero
        }
        return CGSize(width: size.width * scale, height: size.height * scale)
    }

    private func validDrawableSize(from candidates: [CGSize]) -> CGSize {
        for candidate in candidates {
            if isValidDrawableSize(candidate) {
                return candidate
            }
        }
        return CGSize(width: 480, height: 320)
    }

    private func needsDrawableSizeUpdate(_ current: CGSize, _ next: CGSize) -> Bool {
        return abs(current.width - next.width) >= 0.5 || abs(current.height - next.height) >= 0.5
    }

    private func applyDrawableSizeIfNeeded(to view: MTKView, size: CGSize) {
        if needsDrawableSizeUpdate(view.drawableSize, size) {
            view.drawableSize = size
        }

        if let layer = view.layer as? CAMetalLayer, needsDrawableSizeUpdate(layer.drawableSize, size) {
            layer.drawableSize = size
        }
    }

    private func applyDrawableSize(_ size: CGSize, notifyBridge: Bool) {
        guard isValidDrawableSize(size) else {
            NSLog("[Playground] Ignoring invalid resolved drawable size: %.3fx%.3f", size.width, size.height)
            return
        }

        if isApplyingDrawableSize {
            return
        }

        isApplyingDrawableSize = true
        defer {
            isApplyingDrawableSize = false
        }

        applyDrawableSizeIfNeeded(to: mtkView, size: size)
        applyDrawableSizeIfNeeded(to: xrView, size: size)

        if notifyBridge, let appDelegate = UIApplication.shared.delegate as? AppDelegate {
            appDelegate._bridge?.resize(drawableDimension(size.width), height: drawableDimension(size.height))
        }
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
        view.autoResizeDrawable = false
        if isHdr10Run {
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
        view.colorPixelFormat = .bgra8Unorm_srgb
        if let layer = view.layer as? CAMetalLayer {
            layer.pixelFormat = view.colorPixelFormat
            layer.colorspace = CGColorSpace(name: CGColorSpace.extendedLinearDisplayP3)
        }
        #else
        view.colorPixelFormat = .bgra8Unorm_srgb
        #endif
    }
  
    func setupViews() {
        mtkView = MTKView()
        mtkView.translatesAutoresizingMaskIntoConstraints = false
        mtkView.layer.zPosition = 0
        view.addSubview(mtkView)
        let mtkViews = ["mtkView" : mtkView!]
        view.addConstraints(NSLayoutConstraint.constraints(withVisualFormat: "|[mtkView]|", options: [], metrics: nil, views: mtkViews))
        view.addConstraints(NSLayoutConstraint.constraints(withVisualFormat: "V:|[mtkView]|", options: [], metrics: nil, views: mtkViews))
        
        xrView = MTKView()
        configureDrawable(xrView)
        xrView.translatesAutoresizingMaskIntoConstraints = false
        xrView.isUserInteractionEnabled = false
        xrView.isPaused = true
        xrView.enableSetNeedsDisplay = false
        xrView.isOpaque = true
        xrView.alpha = 0.001
        xrView.layer.opacity = 0.001
        xrView.layer.zPosition = 1
        view.addSubview(xrView)
        let xrViews = ["xrView" : xrView!]
        view.addConstraints(NSLayoutConstraint.constraints(withVisualFormat: "|[xrView]|", options: [], metrics: nil, views: xrViews))
        view.addConstraints(NSLayoutConstraint.constraints(withVisualFormat: "V:|[xrView]|", options: [], metrics: nil, views: xrViews))
    }

    private func applyXrPresentation(active xrActive: Bool) {
        guard xrView != nil else { return }

        let alpha: CGFloat = xrActive ? 1.0 : 0.001
        xrView.isHidden = false
        xrView.layer.isHidden = false
        xrView.alpha = alpha
        xrView.layer.opacity = Float(alpha)
        xrView.layer.zPosition = 1

        if xrActive {
            #if swift(>=6.0)
            view.bringSubviewToFront(xrView)
            #else
            view.bringSubview(toFront: xrView)
            #endif
        }

        applyDrawableSize(initialDrawableSize(), notifyBridge: false)

        NSLog("[Playground] XR view active=%d alpha=%.3f hidden=%d layerHidden=%d opacity=%.3f drawable=%.0fx%.0f bounds=%.0fx%.0f window=%d superview=%d z=%.1f",
            xrActive ? 1 : 0,
            xrView.alpha,
            xrView.isHidden ? 1 : 0,
            xrView.layer.isHidden ? 1 : 0,
            xrView.layer.opacity,
            xrView.drawableSize.width,
            xrView.drawableSize.height,
            xrView.bounds.width,
            xrView.bounds.height,
            xrView.window == nil ? 0 : 1,
            xrView.superview == nil ? 0 : 1,
            xrView.layer.zPosition)
    }

    private func updateXrPresentation(active xrActive: Bool) {
        if Thread.isMainThread {
            applyXrPresentation(active: xrActive)
        } else {
            DispatchQueue.main.async { [weak self] in
                self?.applyXrPresentation(active: xrActive)
            }
        }
    }
}

// MARK: MTKViewDelegate
extension ViewController: MTKViewDelegate {
    func draw(in view: MTKView) {
        guard let appDelegate = UIApplication.shared.delegate as? AppDelegate else { return }
        let xrActive = appDelegate._bridge?.isXRActive() ?? false
        if xrActive != lastXrActive {
            lastXrActive = xrActive
            updateXrPresentation(active: xrActive)
        }
        appDelegate._bridge?.render()
    }
    
    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        guard let appDelegate = UIApplication.shared.delegate as? AppDelegate else { return }
        if isApplyingDrawableSize {
            return
        }

        let resolvedSize = validDrawableSize(from: [size, initialDrawableSize()])
        if !isValidDrawableSize(size) {
            NSLog("[Playground] Ignoring invalid drawable size: %.3fx%.3f", size.width, size.height)
        }

        #if os(tvOS)
        NSLog("[Playground] MTKView drawableSizeWillChange=%.0fx%.0f", resolvedSize.width, resolvedSize.height)
        logDisplayState("drawable size changed")
        #endif
        applyDrawableSize(resolvedSize, notifyBridge: false)
        appDelegate._bridge?.resize(drawableDimension(resolvedSize.width), height: drawableDimension(resolvedSize.height))
    }
}
