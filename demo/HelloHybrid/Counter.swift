import UIKit

// Протокол нужен не для красоты: Swift-нативный conformance
// (без @objc — иначе уйдёт в ObjC-таблицы) заставляет swift-frontend
// эмитить секцию __swift5_proto — третий маркер настоящей Swift-компиляции
// в verify_ipa.sh (наряду с __swift5_typeref/__swift5_reflstr).
public protocol Countable {
    func bump() -> Int
}

// Направление Swift→ObjC: Counter дергает ObjC-класс Greeter,
// который виден сюда только через Bridging-Header.h.
@objcMembers
public class Counter: NSObject, Countable {
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
