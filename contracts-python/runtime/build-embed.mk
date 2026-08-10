MICROPYTHON_TOP ?= ../../../../exco-micropython-v128
PACKAGE_DIR ?= build/micropython_embed
BUILD ?= build/embed
include $(MICROPYTHON_TOP)/ports/embed/embed.mk
