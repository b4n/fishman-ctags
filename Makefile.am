
AM_CFLAGS = $(CFLAGS) -Wall
AM_CPPFLAGS = $(CPPFLAGS)			\
	-DDATADIR=\"$(datadir)\"		\
	-DPKGCONFDIR=\"$(pkgconfdir)\"		\
	-DPKGLIBEXECDIR=\"$(pkglibexecdir)\"

# what to do with this??
REGEX_CPPFLAGS = -D_GNU_SOURCE -D__USE_GNU -Dbool=int -Dfalse=0 -Dtrue=1

include $(srcdir)/source.mak

man_MAN	= ctags.1

#
# names for installed man pages
#
manext	= 1
man1dir	= $(mandir)/man1
CMAN	= ctags.$(manext)
EMAN	= etags.$(manext)

#
# corpora files
#
corporadir = $(datadir)/corpora
corpora_DATA = data/corpora/ctags.ctags data/corpora/RFC1213-MIB.txt

#
# preload
#
preloaddir = $(datadir)/preload
pkgconf_preloaddir = $(pkgconfdir)/preload
PRELOAD_OPTLIB =				\
	\
	data/optlib/coffee.ctags				\
	data/optlib/ctags.ctags				\
	data/optlib/mib.ctags				\
	data/optlib/rust.ctags 				\
	\
	$(NULL)
DEFAULT_PRELOAD_FILE=default.ctags

#
# config files
#
optlibdir = $(datadir)/optlib
optlib_DATA = $(PRELOAD_OPTLIB)

#
# drivers
#
driversdir = $(pkglibexecdir)/drivers
drivers_DATA = libexec/drivers/coffeetags


#
# destinations for installed files
#

bin_PROGRAMS = ctags readtags
#~ bin_PROGRAMS += etags
noinst_PROGRAMS = dctags etyperef
lib_LIBRARIES = libreadtags.a
include_HEADERS = readtags.h

ctags_SOURCES = $(sources) $(headers)
#~ etags_SOURCES = $(SOURCES) $(HEADERS)
dctags_SOURCES = debug.c $(sources) $(headers)

readtags_SOURCES = readtags.c readtags.h
readtags_CPPFLAGS = $(AM_CPPFLAGS) -DREADTAGS_MAIN

libreadtags_a_SOURCES = readtags.c readtags.h

etyperef_SOURCES = eiffel.c keyword.c routines.c strlist.c vstring.c
etyperef_CPPFLAGS = $(AM_CPPFLAGS) -DTYPE_REFERENCE_TOOL


DEST_CMAN	= $(man1dir)/$(CMAN)
DEST_EMAN	= $(man1dir)/$(EMAN)


#
# install the executables
#
install-exec-local: install-ebin install-lib install-cmd
install-ebin: $(DEST_ETAGS)

#~ $(DEST_ETAGS):
#~ 	- if [ -x $(DESTDIR)$(DEST_CTAGS) ]; then \
#~ 	    cd $(DESTDIR)$(bindir) && $(SLINK) $(CTAGS_EXEC) $(ETAGS_EXEC); \
#~ 	fi

#
# install the man pages
#
install-data-local: install-eman
install-eman: $(DEST_EMAN)

$(DEST_EMAN):
	- if [ -f $(DESTDIR)$(DEST_CMAN) ]; then \
	    cd $(DESTDIR)$(man1dir) && $(SLINK) $(CMAN) $(EMAN); \
	fi

#
# install data
#
install-data-local: @install_data_targets@
install-data-preload: $(DESTDIR)$(preloaddir) $(DESTDIR)$(pkgconf_preloaddir) FORCE
	echo > $(DESTDIR)$(preloaddir)/$(DEFAULT_PRELOAD_FILE)
	for c in $(PRELOAD_OPTLIB); do \
		echo "--options=$$(basename $$c .ctags)" >> $(DESTDIR)$(preloaddir)/$(DEFAULT_PRELOAD_FILE); \
	done
	chmod 644 $(DESTDIR)$(preloaddir)/$(DEFAULT_PRELOAD_FILE)

#
# rules for uninstalling
#
uninstall-local: uninstall-etags

uninstall-etags:
	- rm -f $(DEST_ETAGS) $(DEST_EMAN)

uninstall-local: uninstall-preload

uninstall-preload:
	- rm -f $(preloaddir)/$(DEFAULT_PRELOAD_FILE)
	- rmdir $(preloaddir)

#
# miscellaneous rules
#
tags: ctags$(EXEEXT)
	./ctags$(EXEEXT) $(srcdir)/*
clean-tags:
	rm -f TAGS

TAGS: ctags$(EXEEXT)
	./ctags$(EXEEXT) -e $(srcdir)/*
clean-TAGS:
	rm -f TAGS

clean-local: clean-tags clean-TAGS

#
# check
#
#~ include $(srcdir)/testing.mak
check: ctags$(EXEEXT)
	$(MAKE) -f testing.mak test.units

# vi:set tabstop=8:
