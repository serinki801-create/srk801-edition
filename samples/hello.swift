// samples/hello.swift — сэмпл Swift + встроенная команда `swift` в AI-OS.
// Хост-сборка (если стоит swiftc): swiftc -O -o hello_swift samples/hello.swift
// На чистом Mint swiftc нет по умолчанию — внутри AI-OS команда `swift`
// выполняет встроенный мини-компилятор выражений в том же синтаксисе.
let langs = ["C", "C++", "ObjC", "Swift"]
let squares = (1...5).map { $0 * $0 }.reduce(0, +)
print("hello from Swift: \(langs.joined(separator: \", \")) (sum sq = \(squares))")
