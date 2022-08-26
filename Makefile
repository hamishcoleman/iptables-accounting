#
#
#

all: iptables-accounting

CFLAGS+=-Wall -Werror

LINT_CCODE+=iptables-accounting.c
LINT_SHELL+=iptables-accounting-add

.PHONY: lint
lint: lint.ccode lint.shell

.PHONY: lint.ccode
lint.ccode:
	uncrustify -c uncrustify.cfg --no-backup ${LINT_CCODE}

.PHONY: lint.shell
lint.shell:
	shellcheck ${LINT_SHELL}

.PHONY: test
test: test.units

.PHONY: test.units
test.units: iptables-accounting test.input test.expected
	./iptables-accounting --test <test.input >test.output
	cmp test.expected test.output
