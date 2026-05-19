import UIKit

#if os(tvOS)
import AVFoundation
import AVKit
import CoreMedia
#endif

@UIApplicationMain
class AppDelegate: UIResponder, UIApplicationDelegate {

    var window: UIWindow?
    var _bridge: LibNativeBridge?

    func application(_ application: UIApplication, didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {
        _bridge = LibNativeBridge()
        #if os(tvOS)
        return true
        #else
        let rootViewController = ViewController()
        rootViewController.view.backgroundColor = .black
        window = UIWindow(frame: UIScreen.main.bounds)
        window?.rootViewController = rootViewController
        window?.makeKeyAndVisible()
        return true
        #endif
    }

    #if os(tvOS)
    func application(_ application: UIApplication, configurationForConnecting connectingSceneSession: UISceneSession, options: UIScene.ConnectionOptions) -> UISceneConfiguration {
        let configuration = UISceneConfiguration(name: "Default Configuration", sessionRole: connectingSceneSession.role)
        configuration.delegateClass = SceneDelegate.self
        return configuration
    }
    #endif

    func applicationWillResignActive(_ application: UIApplication) {
        // Sent when the application is about to move from active to inactive state. This can occur for certain types of temporary interruptions (such as an incoming phone call or SMS message) or when the user quits the application and it begins the transition to the background state.
        // Use this method to pause ongoing tasks, disable timers, and invalidate graphics rendering callbacks. Games should use this method to pause the game.
    }

    func applicationDidEnterBackground(_ application: UIApplication) {
        // Use this method to release shared resources, save user data, invalidate timers, and store enough application state information to restore your application to its current state in case it is terminated later.
        // If your application supports background execution, this method is called instead of applicationWillTerminate: when the user quits.
    }

    func applicationWillEnterForeground(_ application: UIApplication) {
        // Called as part of the transition from the background to the active state; here you can undo many of the changes made on entering the background.
    }

    func applicationDidBecomeActive(_ application: UIApplication) {
        // Restart any tasks that were paused (or not yet started) while the application was inactive. If the application was previously in the background, optionally refresh the user interface.
    }

    func applicationWillTerminate(_ application: UIApplication) {
        // Called when the application is about to terminate. Save data if appropriate. See also applicationDidEnterBackground:.
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
