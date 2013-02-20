#
# Makefile
#
#		NOTE: This top-level Makefile must not
#		use GNU-make extensions. The lower ones can.
#
# Version:	$Id$
#

include Make.inc
MFLAGS += --no-print-directory

# The version of GNU Make is too old, don't use it (.FEATURES variable was
# wad added in 3.81)
ifndef .FEATURES
$(error The build system requires GNU Make 3.81 or later.)
endif

export DESTDIR := $(R)

# And over-ride all of the other magic.
include scripts/boiler.mk

# These are not yet converted to the new system
SUBDIRS		= $(wildcard raddb scripts doc)

.PHONY: tests
tests:
	@$(MAKE) -C src/tests tests

#
# The $(R) is a magic variable not defined anywhere in this source.
# It's purpose is to allow an admin to create an installation 'tar'
# file *without* actually installing it.  e.g.:
#
#  $ R=/home/root/tmp make install
#  $ cd /home/root/tmp
#  $ tar -cf ~/freeradius-package.tar *
#
# The 'tar' file can then be un-tar'd on any similar machine.  It's a
# cheap way of creating packages, without using a package manager.
# Many of the platform-specific packaging tools use the $(R) variable
# when creating their packages.
#
# For compatibility with typical GNU packages (e.g. as seen in libltdl),
# we make sure DESTDIR is defined.
#
export DESTDIR := $(R)

.PHONY: install.bindir
install.bindir:
	@[ -d $(R)$(bindir) ] || $(INSTALL) -d -m 755 $(R)$(bindir)

.PHONY: install.sbindir
install.sbindir:
	@[ -d $(R)$(sbindir) ] || $(INSTALL) -d -m 755 $(R)$(sbindir)

.PHONY: install.raddbdir
install.raddbdir:
	@[ -d $(R)$(raddbdir) ] || $(INSTALL) -d -m 755 $(R)$(raddbdir)

.PHONY: install.dirs
install.dirs: install.bindir install.sbindir
	@$(INSTALL) -d -m 755	$(R)$(mandir)
	@$(INSTALL) -d -m 755	$(R)$(RUNDIR)
	@$(INSTALL) -d -m 700	$(R)$(logdir)
	@$(INSTALL) -d -m 700	$(R)$(radacctdir)
	@$(INSTALL) -d -m 755	$(R)$(datadir)
	@$(INSTALL) -d -m 755	$(R)$(dictdir)

DICTIONARIES := $(wildcard share/dictionary*)
install.share: $(addprefix $(R)$(dictdir)/,$(notdir $(DICTIONARIES)))

$(R)$(dictdir)/%: share/%
	@echo INSTALL $(notdir $<)
	@$(INSTALL) -m 644 $< $@

MANFILES := $(wildcard man/man*/*.?)
install.man: $(subst man/,$(R)$(mandir)/,$(MANFILES))

$(R)$(mandir)/%: man/%
	@echo INSTALL $(notdir $<)
	@$(INSTALL) -m 644 $< $@

install: install.dirs install.share install.man

install:
	@$(MAKE) $(MFLAGS) WHAT_TO_MAKE=$@ common

ifneq ($(RADMIN),)
ifneq ($(RGROUP),)
.PHONY: install-chown
install-chown:
	chown -R $(RADMIN)   $(R)$(raddbdir)
	chgrp -R $(RGROUP)   $(R)$(raddbdir)
	chmod u=rwx,g=rx,o=  `find $(R)$(raddbdir) -type d -print`
	chmod u=rw,g=r,o=    `find $(R)$(raddbdir) -type f -print`
	chown -R $(RADMIN)   $(R)$(logdir)
	chgrp -R $(RGROUP)   $(R)$(logdir)
	find $(R)$(logdir) -type d -exec chmod u=rwx,g=rwx,o= {} \;
	find $(R)$(logdir) -type d -exec chmod g+s {} \;
	find $(R)$(logdir) -type f -exec chmod u=rw,g=rw,o= {} \;
	chown -R $(RADMIN)   $(R)$(RUNDIR)
	chgrp -R $(RGROUP)   $(R)$(RUNDIR)
	find $(R)$(RUNDIR) -type d -exec chmod u=rwx,g=rwx,o= {} \;
	find $(R)$(RUNDIR) -type d -exec chmod g+s {} \;
	find $(R)$(RUNDIR) -type f -exec chmod u=rw,g=rw,o= {} \;
endif
endif

.PHONY: common $(SUBDIRS)

common: $(SUBDIRS)

$(SUBDIRS):
	@echo "Making $(WHAT_TO_MAKE) in $@..."
	@$(MAKE) $(MFLAGS) -C $@ $(WHAT_TO_MAKE)

distclean: clean
	@rm -f config.cache config.log config.status libtool \
		src/include/radpaths.h src/include/stamp-h \
		libltdl/config.log libltdl/config.status \
		libltdl/libtool
	@-find . ! -name configure.in -name \*.in -print | \
		sed 's/\.in$$//' | \
		while read file; do rm -f $$file; done
	@-find src/modules -name config.mak | \
		while read file; do rm -f $$file; done
	@-find src/modules -name config.h | \
		while read file; do rm -f $$file; done

######################################################################
#
#  Automatic remaking rules suggested by info:autoconf#Automatic_Remaking
#
######################################################################
.PHONY: reconfig
reconfig:
	@$(MAKE) $(MFLAGS) -C src reconfig
	@$(MAKE) configure
	@$(MAKE) src/include/autoconf.h.in

configure: configure.in aclocal.m4
	$(AUTOCONF)

.PHONY: src/include/autoconf.h.in
src/include/autoconf.h.in:
	$(AUTOHEADER)

config.status: configure
	./config.status --recheck

configure.in:

.PHONY: check-includes
check-includes:
	scripts/min-includes.pl `find . -name "*.c" -print`

TAGS:
	etags `find src -type f -name '*.[ch]' -print`

#
#  Make test certificates.
#
.PHONY: certs
certs:
	@cd raddb/certs && $(MAKE)

######################################################################
#
#  Make a release.
#
#  Note that "Make.inc" has to be updated with the release number
#  BEFORE running this command!
#
######################################################################
freeradius-server-$(RADIUSD_VERSION_STRING).tar.gz: .git
	git archive --format=tar --prefix=freeradius-server-$(RADIUSD_VERSION_STRING)/ stable | gzip > $@

freeradius-server-$(RADIUSD_VERSION_STRING).tar.gz.sig: freeradius-server-$(RADIUSD_VERSION_STRING).tar.gz
	gpg --default-key aland@freeradius.org -b $<

freeradius-server-$(RADIUSD_VERSION_STRING).tar.bz2: .git
	git archive --format=tar --prefix=freeradius-server-$(RADIUSD_VERSION_STRING)/ stable | bzip2 > $@

freeradius-server-$(RADIUSD_VERSION_STRING).tar.bz2.sig: freeradius-server-$(RADIUSD_VERSION_STRING).tar.bz2
	gpg --default-key aland@freeradius.org -b $<

# high-level targets
.PHONY: dist-check
dist-check: redhat/freeradius.spec suse/freeradius.spec debian/changelog
	@if [ `grep ^Version: redhat/freeradius.spec | sed 's/.*://;s/ //'` != "$(RADIUSD_VERSION_STRING)" ]; then \
		cat redhat/freeradius.spec | sed 's/^Version: .*/Version: $(RADIUSD_VERSION_STRING)/' > redhat/.foo; \
		mv redhat/.foo redhat/freeradius.spec; \
		echo redhat/freeradius.spec 'Version' needs to be updated; \
		exit 1; \
	fi
	@if [ `grep ^Version: suse/freeradius.spec | sed 's/.*://;s/ //'` != "$(RADIUSD_VERSION_STRING)" ]; then \
		cat suse/freeradius.spec | sed 's/^Version: .*/Version: $(RADIUSD_VERSION_STRING)/' > suse/.foo; \
		mv suse/.foo suse/freeradius.spec; \
		echo suse/freeradius.spec 'Version' needs to be updated; \
		exit 1; \
	fi
	@if [ `head -n 1 debian/changelog | sed 's/.*(//;s/-0).*//;s/-1).*//;'`  != "$(RADIUSD_VERSION_STRING)" ]; then \
		echo debian/changelog needs to be updated; \
		exit 1; \
	fi

dist: dist-check freeradius-server-$(RADIUSD_VERSION_STRING).tar.gz freeradius-server-$(RADIUSD_VERSION_STRING).tar.bz2

dist-sign: freeradius-server-$(RADIUSD_VERSION_STRING).tar.gz.sig freeradius-server-$(RADIUSD_VERSION_STRING).tar.bz2.sig

dist-publish: freeradius-server-$(RADIUSD_VERSION_STRING).tar.gz.sig freeradius-server-$(RADIUSD_VERSION_STRING).tar.gz freeradius-server-$(RADIUSD_VERSION_STRING).tar.gz.sig freeradius-server-$(RADIUSD_VERSION_STRING).tar.bz2 freeradius-server-$(RADIUSD_VERSION_STRING).tar.gz.sig freeradius-server-$(RADIUSD_VERSION_STRING).tar.bz2.sig
	scp $^ freeradius.org@ns5.freeradius.org:public_ftp
	scp $^ freeradius.org@www.tr.freeradius.org:public_ftp

#
#  Note that we do NOT do the tagging here!  We just print out what
#  to do!
#
dist-tag: freeradius-server-$(RADIUSD_VERSION_STRING).tar.gz freeradius-server-$(RADIUSD_VERSION_STRING).tar.bz2
	@echo "git tag release_`echo $(RADIUSD_VERSION_STRING) | tr .- __`"

#
#	Build a debian package
#
.PHONY: deb
deb:
	fakeroot dpkg-buildpackage -b -uc

# Developer checks
.PHONY: warnings
warnings:
	@(make clean all 2>&1) | egrep -v '^/|deprecated|^In file included|: In function|   from |^HEADER|^CC|^LINK' > warnings.txt
	@wc -l warnings.txt
