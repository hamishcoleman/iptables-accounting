#
#
#

all: iptables-accounting

LINT_CCODE+=iptables-accounting.c

.PHONY: lint
lint: lint.ccode

.PHONY: lint.ccode
lint.ccode:
	uncrustify -c uncrustify.cfg --no-backup ${LINT_CCODE}

.PHONY: test
test: test.units

.PHONY: test.units
test.units:
	@echo TODO: write tests
	@false
