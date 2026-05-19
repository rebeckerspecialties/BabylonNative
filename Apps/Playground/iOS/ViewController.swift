import UIKit
import MetalKit
import QuartzCore

class ViewController: UIViewController {

    var mtkView: MTKView!
    var xrView: MTKView!

    private let comparisonWidth = 600
    private let comparisonHeight = 400

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
            let bridge = appDelegate._bridge
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
        
        let device = MTLCreateSystemDefaultDevice()
        mtkView.device = device
        
        configureFrameRate(mtkView)
        configureDrawable(mtkView)
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
        
        let initialSize = initialDrawableSize()
        mtkView.drawableSize = initialSize
        let initialWidth = drawableDimension(initialSize.width)
        let initialHeight = drawableDimension(initialSize.height)
        
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

    private func initialDrawableSize() -> CGSize {
        #if os(tvOS)
        let nativeBounds = currentScreen()?.nativeBounds ?? .zero
        if nativeBounds.width > 0 && nativeBounds.height > 0 {
            return nativeBounds.size
        }
        let scale = nativeScale()
        return CGSize(width: max(view.bounds.size.width * scale, 1), height: max(view.bounds.size.height * scale, 1))
        #else
        let scale = view.contentScaleFactor
        return CGSize(width: view.bounds.size.width * scale, height: view.bounds.size.height * scale)
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
        #if os(tvOS)
        if let screen = currentScreen() {
            return screen.nativeScale > 0 ? screen.nativeScale : screen.scale
        }
        return view.contentScaleFactor > 0 ? view.contentScaleFactor : 1
        #else
        return view.contentScaleFactor
        #endif
    }

    private func screenScale() -> CGFloat {
        #if os(tvOS)
        return currentScreen()?.scale ?? nativeScale()
        #else
        return UIScreen.main.scale
        #endif
    }

    private func drawableDimension(_ value: CGFloat) -> Int32 {
        guard value.isFinite && value > 0 else {
            return 1
        }

        return Int32(min(value.rounded(), CGFloat(Int32.max)))
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
  
    func setupViews() {
        mtkView = MTKView()
        mtkView.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(mtkView)
        let mtkViews = ["mtkView" : mtkView!]
        view.addConstraints(NSLayoutConstraint.constraints(withVisualFormat: "|[mtkView]|", options: [], metrics: nil, views: mtkViews))
        view.addConstraints(NSLayoutConstraint.constraints(withVisualFormat: "V:|[mtkView]|", options: [], metrics: nil, views: mtkViews))
        
        xrView = MTKView()
        configureDrawable(xrView)
        xrView.translatesAutoresizingMaskIntoConstraints = false
        xrView.isUserInteractionEnabled = false
        xrView.isHidden = true
        view.addSubview(xrView)
        let xrViews = ["xrView" : xrView!]
        view.addConstraints(NSLayoutConstraint.constraints(withVisualFormat: "|[xrView]|", options: [], metrics: nil, views: xrViews))
        view.addConstraints(NSLayoutConstraint.constraints(withVisualFormat: "V:|[xrView]|", options: [], metrics: nil, views: xrViews))
    }
}

// MARK: MTKViewDelegate
extension ViewController: MTKViewDelegate {
    func draw(in view: MTKView) {
        guard let appDelegate = UIApplication.shared.delegate as? AppDelegate else { return }
        xrView.isHidden = !(appDelegate._bridge?.isXRActive() ?? false)
        appDelegate._bridge?.render()
    }
    
    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        guard let appDelegate = UIApplication.shared.delegate as? AppDelegate else { return }
        guard
            size.width.isFinite,
            size.height.isFinite,
            size.width > 0,
            size.height > 0,
            size.width <= CGFloat(Int32.max),
            size.height <= CGFloat(Int32.max)
        else {
            NSLog("[Playground] Ignoring invalid drawable size: %.3fx%.3f", size.width, size.height)
            return
        }

        appDelegate._bridge?.resize(drawableDimension(size.width), height: drawableDimension(size.height))
    }
}
