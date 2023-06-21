all: examples

DIRS=\
	helloworld\
	helloworld_smp\
	example.001\
	example.002\
	example.003\
	example.004\
	example.005\
	example.006\
	example.007\
	example.008

examples:
	@for dir in $(DIRS) ; do \
		(cd $$dir; make $(MAKECMDGOALS)); \
	done

clean:
	@for dir in $(DIRS) ; do \
		(cd $$dir; make $(MAKECMDGOALS)); \
	done

distclean:
	@find -name "*.o" -exec rm '{}' \;
	@find -name "*~" -exec rm '{}' \;
	@find -name "*.prtosc.c" -exec rm '{}' \;
	@find -name "*.bin" -exec rm '{}' \;
	@find -name "partition?" -exec rm '{}' \;
	@find -name "prtos_cf" -exec rm '{}' \;
	@find -name "resident_sw" -exec rm '{}' \;
	@find -name "resident_sw.iso" -exec rm '{}' \;
	@find -name "dep.mk" -exec rm '{}' \;
