# -*- coding: utf-8 -*-
# main.py — ваш Python-код для iOS (MicroPython).
# Запускается кнопкой RUN из launcher.m.
# Вывод (print) виден в UITextView приложения.

import time

def fib(n):
    a, b = 0, 1
    for _ in range(n):
        a, b = b, a + b
    return a

print("Hello from Python on iOS!")
print("MicroPython, arm64, собран на Linux Mint")

n = 15
print("fib(%d) = %d" % (n, fib(n)))

# demo: простой перебор простых чисел
primes = []
x = 2
while len(primes) < 10:
    is_p = True
    for p in primes:
        if x % p == 0:
            is_p = False
            break
    if is_p:
        primes.append(x)
    x += 1
print("первые 10 простых:", primes)

print("готово")
