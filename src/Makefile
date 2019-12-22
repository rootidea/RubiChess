CXX=g++
CXXFLAGS=-g -std=c++11 -Wall -O3 -flto $(EXTRACXXFLAGS)
ifeq ($(shell uname -m),x86_64)
    ARCHFLAGS=-msse3 -mpopcnt
endif
ifeq ($(shell uname -m),armv7l)
    ARCHFLAGS=-march=armv7-a
endif
LDFLAGS=-pthread  $(EXTRALDFLAGS)

profile_make = gcc-profile-make
profile_use = gcc-profile-use

DEPS = RubiChess.h
EXE = RubiChess
EXE2 = RubiChess-oldcpu
PGOBENCH1 = ./$(EXE) -bench
PGOBENCH2 = ./$(EXE2) -bench
PROFDIR1 = OPT
PROFDIR2 = NOPOCNT

GITVER = $(shell 2>/dev/null git show --name-only --abbrev-commit --date=format:%Y%m%d%H%M%S | grep -i "date" | grep -o -E '[0-9]+')
GITID = $(shell 2>/dev/null git show --name-only --abbrev-commit | grep -i -o -E "ommit[[:blank:]]+[0-9a-f]+" | grep -o -E '[0-9a-f]+')
ifneq ($(GITVER),)
    GITDEFINE = -D GITVER=\"$(GITVER)\"
endif
ifneq ($(GITID),)
    GITDEFINE += -D GITID=\"$(GITID)\"
endif

.PHONY: clean profile-build gcc-profile-make gcc-profile-use all

all: RubiChess
old: RubiChess-oldcpu

RubiChess:
	$(CXX) $(CXXFLAGS) $(EXTRACXXFLAGS) $(ARCHFLAGS) *.cpp $(LDFLAGS) $(EXTRALDFLAGS) $(GITDEFINE) -o $(EXE)

RubiChess-oldcpu:
	$(CXX) $(CXXFLAGS) $(EXTRACXXFLAGS) *.cpp $(LDFLAGS) $(EXTRALDFLAGS) -o $(EXE2)

objclean:
	$(RM) $(EXE) $(EXE2) *.o

profileclean:
	$(RM) -rf $(PROFDIR1)
	$(RM) -rf $(PROFDIR2)

clean: objclean profileclean

gcc-profile-make:
	$(MAKE) EXTRACXXFLAGS='-fprofile-generate=$(PROFDIR1)' EXTRALDFLAGS='-lgcov' $(EXE)
	$(MAKE) EXTRACXXFLAGS='-fprofile-generate=$(PROFDIR2)' EXTRALDFLAGS='-lgcov' $(EXE2)

gcc-profile-use:
	$(MAKE) EXTRACXXFLAGS='-fprofile-use=$(PROFDIR1) -fno-peel-loops -fno-tracer' EXTRALDFLAGS='-lgcov' $(EXE)
	$(MAKE) EXTRACXXFLAGS='-fprofile-use=$(PROFDIR2) -fno-peel-loops -fno-tracer' EXTRALDFLAGS='-lgcov' $(EXE2)

profile-build: clean
	@echo "Building instrumented executable ..."
	$(MAKE) $(profile_make)
	@echo "Running benchmark for pgo-build ..."
	$(PGOBENCH1) > /dev/null
	$(PGOBENCH2) > /dev/null
	@echo "Building optimized executable ..."
	$(MAKE) objclean
	$(MAKE) $(profile_use)
	@echo "Deleting profile data ..."
	$(MAKE) profileclean