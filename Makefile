DIRS=lm4flash \
     lmicdiusb

all::
	@for i in ${DIRS};                  \
	 do                                 \
	     if [ -f $${i}/Makefile ];      \
	     then                           \
	         make -C $${i} || exit $$?; \
	     fi;                            \
	 done

clean::
	@rm -f ${wildcard *~} __dummy__
	@rm -f ${wildcard utils/*~} __dummy__
	@for i in ${DIRS};             \
	 do                            \
	     if [ -f $${i}/Makefile ]; \
	     then                      \
	         make -C $${i} clean;  \
	     fi;                       \
	 done
