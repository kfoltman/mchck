PROG=	usb-midi
USE=	usb

SRCS+=	usb-midi.c spiflash.c

include ../../toolchain/mchck.mk

flashbang: boottoprg flash

boottoprg:
	-amidi --port hw:3,0,0 --send-hex f9

	