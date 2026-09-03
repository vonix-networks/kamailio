#
# db_kazoo module (2600hz): srdb1 driver on a bundled SQLite amalgamation
# (sqlite3.c in this directory; kazoodb/ and kazoodiff/ hold the KazooDB and
# KazooDB-diff command-line tools with their own copies).
#
# WARNING: do not run this directly, it should be run by the main Makefile

include ../../Makefile.defs
auto_gen=
NAME=db_kazoo.so

DEFS += -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION
LIBS += -lm -lpthread

SERLIBPATH=../../lib
SER_LIBS+=$(SERLIBPATH)/srdb1/srdb1

include ../../Makefile.modules
