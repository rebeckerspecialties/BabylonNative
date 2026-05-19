import UIKit

#if os(tvOS)
import AVFoundation
import AVKit
import CoreMedia
#endif

@UIApplicationMain
class AppDelegate: UIResponder, UIApplicationDelegate {

    var window: UIWindow?

    /// Owned by the app: created in `application(_:didFinishLaunchingWithOptions:)`,
    /// torn down in `applicationWillTerminate`. The `ViewController` borrows
    /// this handle to construct its `BNView`.
    var runtime: BNRuntime?

    func application(_ application: UIApplication, didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {
        let runtimeOptions = BNRuntimeOptions()
        runtimeOptions.enableDebugger = true
        runtimeOptions.enableDebugTrace = true
        guard let runtime = BNRuntime(options: runtimeOptions) else {
            fatalError("Failed to construct BNRuntime")
        }

        // Queue the Babylon.js bootstrap scripts and the smoke/validation/user
        // scripts selected by the shared Playground command-line parser. They
        // run after the first BNView attach completes engine initialization on
        // the JS thread, in submission order.
        PlaygroundBootstrap.loadPlaygroundScripts(runtime)

        self.runtime = runtime
        return true
    }

    #if os(tvOS)
    func application(_ application: UIApplication, configurationForConnecting connectingSceneSession: UISceneSession, options: UIScene.ConnectionOptions) -> UISceneConfiguration {
        let configuration = UISceneConfiguration(name: "Default Configuration", sessionRole: connectingSceneSession.role)
        configuration.delegateClass = SceneDelegate.self
        return configuration
    }
    #endif

    func applicationWillResignActive(_ application: UIApplication) {
        runtime?.suspend()
    }

    func applicationDidBecomeActive(_ application: UIApplication) {
        runtime?.resume()
    }

    func applicationWillTerminate(_ application: UIApplication) {
        runtime = nil
    }
}

#if os(tvOS)
class SceneDelegate: UIResponder, UIWindowSceneDelegate {
    var window: UIWindow?

    func scene(_ scene: UIScene, willConnectTo session: UISceneSession, options connectionOptions: UIScene.ConnectionOptions) {
        guard let windowScene = scene as? UIWindowScene else {
            return
        }

        let rootViewController = ViewController()
        rootViewController.view.backgroundColor = .black

        let window = UIWindow(windowScene: windowScene)
        window.rootViewController = rootViewController
        window.makeKeyAndVisible()
        self.window = window

        if let appDelegate = UIApplication.shared.delegate as? AppDelegate {
            appDelegate.window = window
        }

        if CommandLine.arguments.contains("--hdr10") {
            configureDisplayCriteria(for: window)
        }
    }

    private func configureDisplayCriteria(for window: UIWindow) {
        if #available(tvOS 17.0, *) {
            var formatDescription: CMVideoFormatDescription?
            let extensions: [CFString: Any] = [
                kCMFormatDescriptionExtension_ColorPrimaries: kCMFormatDescriptionColorPrimaries_ITU_R_2020,
                kCMFormatDescriptionExtension_TransferFunction: kCMFormatDescriptionTransferFunction_SMPTE_ST_2084_PQ,
                kCMFormatDescriptionExtension_YCbCrMatrix: kCMFormatDescriptionYCbCrMatrix_ITU_R_2020
            ]

            let status = CMVideoFormatDescriptionCreate(
                allocator: kCFAllocatorDefault,
                codecType: kCMVideoCodecType_HEVC,
                width: 3840,
                height: 2160,
                extensions: extensions as CFDictionary,
                formatDescriptionOut: &formatDescription
            )

            if status == noErr, let formatDescription {
                window.avDisplayManager.preferredDisplayCriteria = AVDisplayCriteria(
                    refreshRate: 60,
                    formatDescription: formatDescription
                )
                NSLog("[Playground] Requested tvOS 4K HDR10 display criteria.")
            } else {
                NSLog("[Playground] Failed to create tvOS HDR display criteria: %d", status)
            }
        }
    }
}
#endif
