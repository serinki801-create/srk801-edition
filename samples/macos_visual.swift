// samples/macos_visual.swift — мини-визуал в стиле macOS (glass + dock),
// рассчитан на хост-компилятор `swift` (Swift.org, swiftlang.com) с GNUstep
// или с обычным `swiftc` из toolchain'а. Внутри AI-OS тот же файл исполняется
// нашим встроенным компилятором для арифметики (cc/cxx/objc/swift).
let title = "AI-OS Glass"
let dock = ["Finder", "Term", "Sys", "Exec", "Trash"]
print("[macOS-visual] title='\(title)' opacity=0.65 dock=\(dock.count)")
for (i, name) in dock.enumerated() {
    print(String(format: "[macOS-visual] dock[%d] %@", i, name))
}
print("[macOS-visual] OK")
