# ------------------------------------------------------------------------------
# autotools

SHELL = bash
ACLOCAL_AMFLAGS = -I build/m4 ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory
AUTOMAKE_OPTIONS = color-tests parallel-tests

GCC_COLORS ?= 'ooh, shiny!'
export GCC_COLORS

# remove targets if the command fails
.DELETE_ON_ERROR:

# keep intermediate files
.SECONDARY:

# keep the test-suite.log and Makefile around at all times
.PRECIOUS: $(TEST_SUITE_LOG) Makefile

pkgconfiglibdir=$(libdir)/pkgconfig
bashcompletiondir = $(datadir)/bash-completion/completions

include_HEADERS =
pkgconfiglib_DATA =
bin_PROGRAMS =
noinst_PROGRAMS =
noinst_LIBRARIES =
dist_bashcompletion_DATA =
check_PROGRAMS =
TESTS=

BUILT_SOURCES =
CLEANFILES = $(BUILT_SOURCES)
EXTRA_DIST =

AM_CPPFLAGS = \
	-include $(top_builddir)/build/config.h \
	-I $(top_srcdir)/lib \
	-I $(top_builddir)/lib \
	$(OUR_CPPFLAGS)

AM_CFLAGS = $(OUR_CFLAGS)
AM_LDFLAGS = $(OUR_LDFLAGS)

# ------------------------------------------------------------------------------
# varlink interface files

%.varlink.inc.c: %.varlink
	$(AM_V_GEN) \
		if [ -x /usr/bin/varlink ]; then varlink format $< >/dev/null; fi && \
		echo "static const char *$(subst .,_,$(patsubst %.h,%,$(notdir $<))) = R\"INTERFACE(" > $@ && \
		cat $< >> $@ && \
		echo ")INTERFACE\";" >> $@

# ------------------------------------------------------------------------------
# library archive

noinst_LIBRARIES += \
	libvarlink.a

libvarlink_a_SOURCES = \
	lib/array.c \
	lib/array.h \
	lib/avltree.c \
	lib/avltree.h \
	lib/connection.c \
	lib/error.c \
	lib/error.h \
	lib/interface.c \
	lib/interface.h \
	lib/object.c \
	lib/object.h \
	lib/parse-error.c \
	lib/parse-error.h \
	lib/scanner.c \
	lib/scanner.h \
	lib/socket.c \
	lib/socket.h \
	lib/socket-tcp.c \
	lib/socket-unix.c \
	lib/service.c \
	lib/service.h \
	lib/util.c \
	lib/util.h \
	lib/value.c \
	lib/value.h \
	lib/varlink.h

libvarlink_a_CFLAGS = \
	$(AM_CFLAGS)

# ------------------------------------------------------------------------------
# shared library built from archive

all-local: libvarlink.so.0

include_HEADERS += \
	lib/varlink.h

libvarlink.so.0: libvarlink.a $(top_srcdir)/lib/libvarlink.sym
	$(AM_V_CCLD)$(LINK) -shared \
	-Wl,-soname=$@ \
	-Wl,--version-script=$(top_srcdir)/lib/libvarlink.sym \
	-Wl,--whole-archive libvarlink.a -Wl,--no-whole-archive

CLEANFILES += \
	libvarlink.so.0

install-exec-local:
	@echo " $(MKDIR_P) '$(DESTDIR)$(libdir)'"; \
		$(MKDIR_P) "$(DESTDIR)$(libdir)" || exit 1
	@echo " $(INSTALL) libvarlink.so.0 '$(DESTDIR)$(libdir)'"; \
		$(INSTALL) libvarlink.so.0 "$(DESTDIR)$(libdir)" || exit $$?
	@echo " $(LN_S) -f libvarlink.so.0 '$(DESTDIR)$(libdir)/libvarlink.so'"; \
		$(LN_S) -f libvarlink.so.0 "$(DESTDIR)$(libdir)/libvarlink.so" || exit $$?

uninstall-local:
	@test ! -d "$(DESTDIR)$(libdir)" || \
		{ echo " ( cd '$(DESTDIR)$(libdir)' && rm -f libvarlink.so.0 )"; \
		  rm -f "$(DESTDIR)$(libdir)/libvarlink.so.0"; }

%.pc: %.pc.in
	$(AM_V_GEN)$(SED) \
		-e 's,@includedir\@,$(includedir),g' \
		-e 's,@libdir\@,$(libdir),g' \
		-e 's,@VERSION\@,$(VERSION),g' \
		$< > $@

pkgconfiglib_DATA += \
	lib/libvarlink.pc

BUILT_SOURCES += \
	lib/org.varlink.service.varlink.inc.c

TESTS += \
	lib/test-symbols.sh

EXTRA_DIST += \
	lib/libvarlink.sym \
	lib/org.varlink.service.varlink \
	lib/libvarlink.pc.in \
	lib/test-symbols.sh

CLEANFILES += \
	lib/libvarlink.pc

# ------------------------------------------------------------------------------
# test-interface

check_PROGRAMS += test-interface

test_interface_SOURCES = \
	lib/test-interface.c

test_interface_LDADD = \
	libvarlink.a

# ------------------------------------------------------------------------------
# test-server-client

check_PROGRAMS += test-server-client

test_server_client_SOURCES = \
	lib/test-server-client.c

test_server_client_LDADD = \
	libvarlink.a

# ------------------------------------------------------------------------------
# test-object

check_PROGRAMS += test-object

test_object_SOURCES = \
	lib/test-object.c

test_object_LDADD = \
	libvarlink.a

# ------------------------------------------------------------------------------
# test-array

check_PROGRAMS += test-array

test_array_SOURCES = \
	lib/test-array.c

test_array_LDADD = \
	libvarlink.a

# ------------------------------------------------------------------------------
# test-interface-types

check_PROGRAMS += test-interface-type

test_interface_type_SOURCES = \
	lib/test-interface-type.c

test_interface_type_LDADD = \
	libvarlink.a

# ------------------------------------------------------------------------------
# test-error

check_PROGRAMS += test-error

test_error_SOURCES = \
	lib/test-error.c

test_error_LDADD = \
	libvarlink.a

# ------------------------------------------------------------------------------
# test-avl
check_PROGRAMS += test-avl

test_avl_SOURCES = \
	lib/test-avl.c

test_avl_LDADD = \
	libvarlink.a

# ------------------------------------------------------------------------------
# command line tool
bin_PROGRAMS += \
	varlink

varlink_SOURCES = \
	tool/cli.c \
	tool/cli.h \
	tool/command.c \
	tool/command.h \
	tool/command-call.c \
	tool/command-complete.c \
	tool/command-error.c \
	tool/command-format.c \
	tool/command-help.c \
	tool/command-info.c \
	tool/command-resolve.c \
	tool/main.c \
	tool/terminal-colors.c \
	tool/terminal-colors.h

varlink_CFLAGS = \
	$(AM_CFLAGS)

varlink_LDADD = \
	libvarlink.a

dist_bashcompletion_DATA += \
	bash-completion/varlink

# ------------------------------------------------------------------------------¶
# test suite

AM_TESTS_ENVIRONMENT = LD_LIBRARY_PATH=$(abs_builddir)
TESTS += $(check_PROGRAMS)

# ------------------------------------------------------------------------------
if HAVE_ASTYLE
check-format:
	@for f in lib/*.[ch] tool/*.[ch]; do \
		echo "  CHECK-FORMAT $$f"; \
		$(ASTYLE) --quiet --options=$(abs_srcdir)/.astylerc < $$f | cmp -s $$f -; \
		if [ $$? -ne 0 ]; then \
			$(ASTYLE) --quiet --options=$(abs_srcdir)/.astylerc < $$f | diff -u $$f -; \
			exit 1; \
		fi; \
	done

format:
	@for f in lib/*.[ch] tool/*.[ch]; do \
		echo "  FORMAT $$f"; \
		$(ASTYLE) --quiet --options=$(abs_srcdir)/.astylerc $$f; \
	done
endif
.PHONY: check-format
.PHONY: format

if HAVE_VALGRIND
check-valgrind: $(check_PROGRAMS)
	@for f in $^; do \
		echo "  VALGRIND $$f"; \
		valgrind --quiet --leak-check=full --error-exitcode=1 $(builddir)/$$f || exit $$?; \
	done
check: check-valgrind
endif
.PHONY: check-valgrind

install-tree: all
	rm -rf $(abs_builddir)/install-tree
	$(MAKE) install DESTDIR=$(abs_builddir)/install-tree
	tree $(abs_builddir)/install-tree
.PHONY: install-tree
