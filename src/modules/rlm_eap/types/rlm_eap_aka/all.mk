TARGETNAME	:= rlm_eap_aka

ifneq "$(OPENSSL_LIBS)" ""
TARGET		:= $(TARGETNAME).a
endif

SOURCES		:= $(TARGETNAME).c

SRC_CFLAGS	:=
TGT_LDLIBS	:=
SRC_INCDIRS	:= ${top_srcdir}/src/modules/rlm_eap/ ${top_srcdir}/src/modules/rlm_eap/lib/base/ ${top_srcdir}/src/modules/rlm_eap/lib/sim/

TGT_PREREQS	:= libfreeradius-eap.a libfreeradius-eap-sim.a
