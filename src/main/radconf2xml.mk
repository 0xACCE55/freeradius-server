TARGET		:= radconf2xml
SOURCES		:= radconf2xml.c util.c log.c conffile.c

TGT_PREREQS	:= libfreeradius-radius.a
TGT_LDLIBS	:= $(LIBS)
