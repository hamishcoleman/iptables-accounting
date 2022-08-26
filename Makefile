#
#
#

all: iptables-accounting

CFLAGS+=-Wall -Werror

LINT_CCODE+=iptables-accounting.c
LINT_SHELL+=iptables-accounting-add

BUILD_DEP+=uncrustify
BUILD_DEP+=yamllint

.PHONY: build-dep
build-dep:
	sudo apt-get -y install $(BUILD_DEP)

.PHONY: lint
lint: lint.ccode lint.shell lint.yaml

.PHONY: lint.ccode
lint.ccode:
	uncrustify -c uncrustify.cfg --no-backup ${LINT_CCODE}

.PHONY: lint.shell
lint.shell:
	shellcheck ${LINT_SHELL}

.PHONY: lint.yaml
lint.yaml:
	yamllint .

.PHONY: test
test: test.units

.PHONY: test.units
test.units: iptables-accounting test.input test.expected
	./iptables-accounting --test <test.input >test.output
	cmp test.expected test.output
