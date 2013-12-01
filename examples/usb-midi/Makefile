PROG=	usb-midi
USE=	usb

SRCS+=	usb-midi.c uart.c seq.c

include ../../toolchain/mchck.mk

flashbang: boottoprg flash

boottoprg:
	python reset.py

	