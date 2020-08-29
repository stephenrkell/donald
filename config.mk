THIS_MAKEFILE := $(lastword $(MAKEFILE_LIST))
$(warning THIS_MAKEFILE is $(THIS_MAKEFILE))

FINDLIB_PREFIX ?= $(realpath $(dir $(THIS_MAKEFILE)))/contrib/findlib-1.4.1/install
OCAML_PREFIX ?= $(realpath $(dir $(THIS_MAKEFILE)))/contrib/ocaml-4.01.0/install

export PATH := $(OCAML_PREFIX)/bin:$(FINDLIB_PREFIX)/bin:${PATH}
export OCAMLPATH := $(OCAML_PREFIX)/lib/ocaml 
# -- don't include the existing OCAMLPATH! :${OCAMLPATH}
# bitstring needs OCAMLLIB set
export OCAMLLIB := $(OCAML_PREFIX)/lib/ocaml

export ELF_LEM := $(realpath $(dir $(THIS_MAKEFILE))/contrib/system-v-abi/src)
export BATTERIES := $(ELF_LEM)/src_elf_libraries/batteries/_build/src/
export BITSTRING := $(ELF_LEM)/src_elf_libraries/bitstring/
export UINT := $(ELF_LEM)/src_elf_libraries/uint/_build/lib/

export CFLAGS += -I$(UINT)/../../lib
export LEM_OCAML_LIB := $(realpath $(dir $(THIS_MAKEFILE))/contrib/lem/ocaml-lib/_build)

.PHONY: env
env:
	@echo PATH=$(PATH)
	@echo OCAMLPATH=$(OCAMLPATH)
	@echo OCAMLLIB=$(OCAMLLIB)
	@echo OCAMLFIND_CONF=$(shell pwd)/findlib.conf
