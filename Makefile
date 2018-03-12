all:
	@${MAKE} -C lmicdiusb all
	@${MAKE} -C lm4flash all

install: all
	@${MAKE} -C lmicdiusb install
	@${MAKE} -C lm4flash install
