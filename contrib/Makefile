default: lem system-v-abi

include ../config.mk
THIS_MAKEFILE := $(lastword $(MAKEFILE_LIST))

OCAML_BUILT_FILE := ocaml-4.01.0/ocamlopt.opt
OCAML_INSTALLED_FILE := $(OCAML_PREFIX)/bin/ocamlopt.opt

ocaml-4.01.0: ocaml-4.01.0.tar.gz ocaml-pic.patch 
	test -d "$@" || tar xzf ocaml-4.01.0.tar.gz
	touch "$@"
	cd "$@" && patch -p1 < ../ocaml-pic.patch

ocaml-4.01.0.tar.gz:
	wget -N -O ocaml-4.01.0.tar.gz "http://caml.inria.fr/pub/distrib/ocaml-4.01/ocaml-4.01.0.tar.gz"

$(OCAML_BUILT_FILE): ocaml-4.01.0
	cd ocaml-4.01.0 && ./configure -prefix $(OCAML_PREFIX)
	$(MAKE) -C "$<" world.opt
	# Now the ocaml directory probably has a more recent timestamp than the built file.
	# This will cause spurious rebuilds if we don't fix it.
	touch "$@"

$(OCAML_INSTALLED_FILE): $(OCAML_BUILT_FILE)
	cd ocaml-4.01.0 && $(MAKE) install
	# debugging version skew issues: re-make ocaml using itself (SHOULDN'T be necessary)
	PATH=$(OCAML_PREFIX)/bin:${PATH} $(MAKE) -C ocaml-4.01.0 clean
	PATH=$(OCAML_PREFIX)/bin:${PATH} $(MAKE) -C ocaml-4.01.0

FINDLIB_PREFIX := $(shell pwd)/findlib-1.4.1/install
FINDLIB_INSTALLED_FILE := $(FINDLIB_PREFIX)/bin/ocamlfind
FINDLIB_BUILT_FILE := findlib-1.4.1/src/findlib/ocamlfind

# Quick shell hack for trying out failing build commands:
# $ env $(make env) sh -c 'cd /var/local/stephen/work/devel/donald.hg/contrib/system-v-abi/src/src_elf_libraries/batteries && make'

# Since some of our subrepos will use ocamlfind, we need our own findlib.conf
findlib.conf:
	echo 'destdir="$(OCAML_PREFIX)/lib" \
path="$(OCAML_PREFIX)/lib/ocaml" \
ocamlc="$(OCAML_PREFIX)/bin/ocamlc.opt" \
ocamlopt="$(OCAML_PREFIX)/bin/ocamlopt.opt" \
ocamldep="$(OCAML_PREFIX)/bin/ocamldep.opt" \
ocamldoc="$(OCAML_PREFIX)/bin/ocamldoc.opt"' | sed 's/\\$$//' > "$@"

# ... in fact, we need our own findlib, since it has the META files for 
# all the standard OCaml packages. 
# Current version of findlib as of 2014/11/24, version 1.5.5, for some reason
# doesn't support ocamldep. So stick with known-good 1.4.1.
findlib-1.4.1.tar.gz: 
	wget -N -O "$@" http://download.camlcity.org/download/findlib-1.4.1.tar.gz

findlib-1.4.1: findlib-1.4.1.tar.gz
	test -d "$@" || tar xzf "$<"
	touch "$@"

# Build findlib just so we have the meta files.
# It will try to use the broken (non-opt) ocamlc et al, so override its Makefile.
FINDLIB_OVERRIDES := OCAMLC="ocamlc.opt -I +compiler-libs" OCAMLOPT="ocamlopt.opt -I +compiler-libs" OCAMLDEP=ocamldep.opt OCAMLLEX=ocamllex.opt
$(FINDLIB_BUILT_FILE): findlib-1.4.1
	mkdir -p $(FINDLIB_PREFIX)
	cd findlib-1.4.1 && \
./configure \
-bindir $(FINDLIB_PREFIX)/bin \
-mandir $(FINDLIB_PREFIX)/man \
-sitelib $(FINDLIB_PREFIX)/lib \
-config $(shell pwd)/findlib.conf \
&& $(MAKE) $(FINDLIB_OVERRIDES) all && $(MAKE) $(FINDLIB_OVERRIDES) opt

$(FINDLIB_INSTALLED_FILE): $(FINDLIB_BUILT_FILE)
	cd findlib-1.4.1 && $(MAKE) install

.PHONY: META
META: $(FINDLIB_INSTALLED_FILE)
	mkdir -p META && find $(dir $(FINDLIB_INSTALLED_FILE)).. -name 'META' | while read line; do \
		outfile="$(OCAML_PREFIX)/lib/ocaml/$$( echo "$$line" | sed 's^.*/\([-a-z0-9]*\)/META^META.\1^' )"; \
		cat "$$line" > "$$outfile"; \
		grep '^directory = ' "$$outfile" >/dev/null || (echo "directory = \"$(OCAML_PREFIX)/lib/ocaml\"" >> "$$outfile"); \
	done

.PHONY: lem
lem: findlib.conf | $(OCAML_INSTALLED_FILE)
	OCAMLFIND_CONF=$(shell pwd)/findlib.conf $(MAKE) -C lem
	OCAMLFIND_CONF=$(shell pwd)/findlib.conf $(MAKE) -C lem ocaml-libs

.PHONY: system-v-abi
system-v-abi: findlib.conf lem | $(OCAML_INSTALLED_FILE)
	OCAMLFIND_CONF=$(shell pwd)/findlib.conf $(MAKE) -C system-v-abi/src

clean-local:
	$(MAKE) -C system-v-abi/src clean
	$(MAKE) -C lem clean
	$(MAKE) -C lem/library clean
	$(MAKE) -C lem/ocaml-lib clean

clean: clean-local

cleanall: clean-local
	$(MAKE) -C ocaml-4.01.0 clean
	$(MAKE) -C findlib-1.4.1 clean
