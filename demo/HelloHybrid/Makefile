export TARGET = iphone:clang:latest:13.0
export ARCHS = arm64

include $(THEOS)/makefiles/common.mk

APPLICATION_NAME = HelloHybrid
HelloHybrid_FILES = $(wildcard *.m)
# Эта версия Theos требует SWIFT_FILES отдельно от FILES (см. instance/rules.mk:
# правило компиляции Swift читает именно $(SWIFT_FILES)); оба списка линкуются
# в один бинарь таргета.
HelloHybrid_SWIFT_FILES = $(wildcard *.swift)
HelloHybrid_FRAMEWORKS = UIKit Foundation CoreGraphics
HelloHybrid_CFLAGS = -fobjc-arc
HelloHybrid_SWIFTFLAGS = -import-objc-header $(PWD)/Bridging-Header.h \
                         -target arm64-apple-ios13.0
HelloHybrid_INSTALL_TARGET_PROCESSES = HelloHybrid
HelloHybrid_CODESIGN_FLAGS = -S
HelloHybrid_RESOURCE_FILES = Info.plist

include $(THEOS_MAKE_PATH)/application.mk
