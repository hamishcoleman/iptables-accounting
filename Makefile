#
#
#

all: iptables-accounting

CFLAGS+=-Wall -Wextra -Werror -g

ifneq ($(NOANALYZER),1)
CFLAGS+=-fanalyzer
endif

ifdef SANITISE
CFLAGS+=-fsanitize=leak
LDFLAGS+=-fsanitize=leak
endif

LINT_CCODE+=iptables-accounting.c
LINT_CCODE+=strbuf.c strbuf.h strbuf-tests.c
LINT_CCODE+=connslot.c connslot.h
LINT_CCODE+=httpd-test.c
LINT_SHELL+=iptables-accounting-add

BUILD_DEP+=uncrustify
BUILD_DEP+=yamllint

CLEAN+=iptables-accounting
CLEAN+=strbuf-tests
CLEAN+=*.o

strbuf.o: strbuf.h
strbuf-tests: strbuf.o
connslot.o: connslot.h
httpd-test: connslot.o strbuf.o

iptables-accounting: strbuf.o connslot.o

.PHONY: build-dep
build-dep:
	sudo apt-get -y install $(BUILD_DEP)

.PHONY: lint
lint: lint.ccode lint.shell lint.yaml

.PHONY: lint.ccode
lint.ccode:
	uncrustify -c uncrustify.cfg --check ${LINT_CCODE}

.PHONY: lint.shell
lint.shell:
	shellcheck ${LINT_SHELL}

.PHONY: lint.yaml
lint.yaml:
	yamllint .

.PHONY: test
test: test.strbuf
test: test.unit

.PHONY: test.strbuf
test.strbuf: strbuf-tests
	./strbuf-tests

.PHONY: test.unit
test.unit: iptables-accounting test.input test.expected
	./iptables-accounting --test <test.input >test.output
	cmp test.expected test.output

.PHONY: clean
clean:
	rm -f ${CLEAN}
