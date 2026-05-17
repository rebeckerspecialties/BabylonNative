import UIKit

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
