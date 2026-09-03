#
# db_kazoo module (2600hz): srdb1 driver on a bundled SQLite amalgamation
# (sqlite3.c in this directory). kazoodb/ and kazoodiff/ hold the KazooDB
# (sqlite shell) and KazooDB-diff (sqldiff) tools the kazoo-configs-kamailio
# db_scripts use to create and upgrade the database; built and installed
# alongside the module.
#
# WARNING: do not run this directly, it should be run by the main Makefile

include ../../Makefile.defs
auto_gen=
NAME=db_kazoo.so

DEFS += -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION
LIBS += -lm -lpthread

SERLIBPATH=../../lib
SER_LIBS+=$(SERLIBPATH)/srdb1/srdb1

KAZOODB=kazoodb/KazooDB
KAZOODIFF=kazoodiff/KazooDB-diff
TOOL_CFLAGS=-O2 -g -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_ENABLE_JSON1 -DSQLITE_ENABLE_FTS4

include ../../Makefile.modules

$(KAZOODB): kazoodb/shell.c kazoodb/sqlite3.c kazoodb/linenoise.c
	@echo "Building $@"
	$(CC) $(TOOL_CFLAGS) -DHAVE_LINENOISE=1 -o $@ $^ -lm -lpthread -ldl

$(KAZOODIFF): kazoodiff/sqldiff.c kazoodiff/sqlite3.c
	@echo "Building $@"
	$(CC) $(TOOL_CFLAGS) -o $@ $^ -lm -lpthread -ldl

db_kazoo.so: $(KAZOODB) $(KAZOODIFF)

ifeq ($(INSTALL_FLAVOUR),kamailio)
install-kazoodb-tools: $(KAZOODB) $(KAZOODIFF) $(bin_prefix)/$(bin_dir)
	$(INSTALL_TOUCH) $(bin_prefix)/$(bin_dir)/KazooDB $(bin_prefix)/$(bin_dir)/KazooDB-diff
	$(INSTALL_BIN) $(KAZOODB) $(bin_prefix)/$(bin_dir)/KazooDB
	$(INSTALL_BIN) $(KAZOODIFF) $(bin_prefix)/$(bin_dir)/KazooDB-diff

install-kazoodb-scripts: $(bin_prefix)/$(bin_dir)
	DBKAZOOON=yes $(MAKE) -C ../../../utils/kamctl/ install-modules

install-scripts: install-kazoodb-tools install-kazoodb-scripts
endif

modclean:
	-@rm -f $(KAZOODB) $(KAZOODIFF)

clean clean-modules: modclean

proper realclean distclean maintainer-clean: modclean
