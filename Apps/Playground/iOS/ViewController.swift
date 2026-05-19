import UIKit
import MetalKit
import QuartzCore

class ViewController: UIViewController {

    var mtkView: MTKView!
    var xrView: MTKView!
    var bnView: BNView?

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
        guard
            let appDelegate = UIApplication.shared.delegate as? AppDelegate,
            let runtime = appDelegate.runtime
        else { return }

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

    private func currentScreen() -> UIScreen? {
        #if os(tvOS)
        return view.window?.windowScene?.screen
        #else
        return view.window?.screen ?? UIScreen.main
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
