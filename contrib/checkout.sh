#!/bin/bash

test -d ocaml-4.01.0 || (wget -N -O ocaml-4.01.0.tar.gz "http://caml.inria.fr/pub/distrib/ocaml-4.01/ocaml-4.01.0.tar.gz" \
 && tar xzf ocaml-4.01.0.tar.gz && cd ocaml-4.01.0 && ./configure -prefix `pwd`/install \
 && patch -p1 <<"EOF"
--- ocaml/asmrun/Makefile    2013-06-24 09:16:27.000000000 +0100
+++ ocaml/asmrun/Makefile        2014-10-09 17:02:05.142709250 +0100
@@ -14,7 +14,8 @@
 include ../config/Makefile
 
 CC=$(NATIVECC)
-FLAGS=-I../byterun -DCAML_NAME_SPACE -DNATIVE_CODE \
+# HACKed by donald's contrib getter
+FLAGS=-I../byterun -fPIC -DCAML_NAME_SPACE -DNATIVE_CODE \
       -DTARGET_$(ARCH) -DSYS_$(SYSTEM) $(IFLEXDIR)
 CFLAGS=$(FLAGS) -O $(NATIVECCCOMPOPTS)
 DFLAGS=$(FLAGS) -g -DDEBUG $(NATIVECCCOMPOPTS)
EOF
)
(test -d ocaml-4.01.0/install || (make -C ocaml-4.01.0 world.opt && make -C ocaml-4.01.0 install))

export PATH="`pwd`/ocaml-4.01.0/install/bin:${PATH}"
export OCAMLPATH="`pwd`/ocaml-4.01.0/install/lib/ocaml:"
# bitstring needs OCAMLLIB set
export OCAMLLIB="`pwd`/ocaml-4.01.0/install/lib/ocaml"
echo ocaml is `which ocaml` 1>&2
echo ocamlc is `which ocamlc` 1>&2
echo ocamlfind is `which ocamlfind` 1>&2
echo ocamlfind says ocamlc version is `ocamlfind ocamlc -version` 1>&2

#test -d batteries-included || (git clone https://github.com/ocaml-batteries-team/batteries-included.git && \
#    cd batteries-included && git checkout release-2.2.0)
#(cd batteries-included && make all)

#test -d bitstring || (git clone https://code.google.com/p/bitstring/ && \
#	cd bitstring && git checkout master)
#(cd bitstring && (test -e config.h || (aclocal && autoreconf && ./configure)) && \
#make srcdir='$(top_srcdir)' )

# To fix "-fno-defer-pop" build problem on Mac OS, brew install gcc
# and make sure "gcc" runs the brew version (not clang). Or get ocaml
# 4.01.0 via opam, which avoids this problem.
#test -d ocaml-uint || (git clone https://github.com/andrenth/ocaml-uint.git && \
#	cd ocaml-uint && git checkout 1.1.x) && \
#(cd ocaml-uint && make configure && make)

# lem
test -d lem || (echo "Error: no lem" 1>&2; false) || exit 1
(cd lem && make)

# system-v-abi
test -d system-v-abi || (echo "Error: no system-v-abi" 1>&2; false) || exit 1
(cd system-v-abi/src && make lem-model && make -C src_elf_libraries && make ocaml)
