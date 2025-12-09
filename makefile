# 
# Tiny C Preprocessor
# Copyright (c) 2025 mzuhi5
#

SHELL=/bin/bash

prep: prep.c
	gcc -o $@ -fno-builtin -fno-gnu-unique -O0 -g -Wall $^

prep_self: prep_self.c
	gcc -o $@ -fno-builtin -fno-gnu-unique -O0 -g -Wall $^

prep_self.c: prep
	./prep prep.c > $@

test: prep
	@LIST=`ls test/*.h`; \
	for file in $$LIST ;\
	do \
		diff -w -B <(gcc -E -P $$file) <(./prep $$file) ; \
		if [[ $$? -eq 0 ]] then \
			echo "PASS: $$file"; \
		else \
			echo "FAIL: $$file"; \
		fi \
	done

test_self: prep_self
	@echo "checking diff between prep_self.c and output from prep_self."; \
	./prep_self prep.c > prep_self_test.c; \
	diff -w -B prep_self.c prep_self_test.c  > /dev/null; \
	if [[ $$? -eq 0 ]] then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
	fi

clean:
	rm -f prep prep_self a.out prep_self.c prep_self_test.c

.PHONY: clean test test_self