import UIKit

// Swift-шаблон: приложение целиком на Swift + UIKit.
// @UIApplicationMain — точка входа, генерирует main() и UIApplicationMain.

@UIApplicationMain
class AppDelegate: UIResponder, UIApplicationDelegate {
    var window: UIWindow?

    func application(_ app: UIApplication,
                     didFinishLaunchingWithOptions options: [UIApplication.LaunchOptionsKey: Any]? = nil) -> Bool {
        let window = UIWindow(frame: UIScreen.main.bounds)
        window.rootViewController = SwiftVC()
        window.makeKeyAndVisible()
        self.window = window
        return true
    }
}

class SwiftVC: UIViewController {
    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemIndigo

        let n = 30
        let value = factorial(n)
        let label = UILabel(frame: CGRect(x: 20, y: 200, width: 340, height: 240))
        label.numberOfLines = 0
        label.font = .systemFont(ofSize: 20)
        label.textColor = .white
        label.text = """
        Hello from Swift!
        \(n)! = \(value)
        Swift + UIKit, iOS arm64
        """
        view.addSubview(label)
    }
}

func factorial(_ n: Int) -> Int {
    (1...n).reduce(1, *)
}
