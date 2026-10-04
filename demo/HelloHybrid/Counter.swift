import UIKit

// Направление Swift→ObjC: Counter дергает ObjC-класс Greeter,
// который виден сюда только через Bridging-Header.h.
@objcMembers
public class Counter: NSObject {
    @objc public var n: Int = 0

    @objc public func bump() -> Int {
        n += 1
        return n
    }

    @objc public static func describe() -> String {
        return "swift says: " + Greeter.greeting()
    }

    @objc public static func answer() -> Int { return 42 }
}
