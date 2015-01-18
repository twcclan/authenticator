#
# TWC Extras Makefile
# adapted from GMSM_QMM Makefile http://maintain-et.net
# 

include Config.mak

CC=g++

BASE_CFLAGS=-pipe -m32 -I./include -lcurl -lidn -lssl -lcrypto -lrt -lssl -lcrypto

BROOT=linux
BR=$(BROOT)/release
BD=$(BROOT)/debug

B_Q3A=
B_RTCWET=_rtcwet

OBJR_Q3A=$(SRC_FILES:%.cpp=$(BR)$(B_Q3A)/%.o)
OBJD_Q3A=$(SRC_FILES:%.cpp=$(BD)$(B_Q3A)/%.o)
OBJR_RTCWET=$(SRC_FILES:%.cpp=$(BR)$(B_RTCWET)/%.o)
OBJD_RTCWET=$(SRC_FILES:%.cpp=$(BD)$(B_RTCWET)/%.o)

DEBUG_CFLAGS=-g -Wl,-no-as-needed $(BASE_CFLAGS) --verbose
RELEASE_CFLAGS=-Wl,-no-as-needed $(BASE_CFLAGS) -DNDEBUG -O2 -pipe -w -fomit-frame-pointer -ffast-math -falign-loops=2 -falign-jumps=2 -falign-functions=2 -fno-strict-aliasing -fstrength-reduce

SHLIBCFLAGS=-fPIC
SHLIBLDFLAGS=-shared -m32

default: rtcwet

help:
	@echo TWC Authenticator supports the following make rules:
	@echo rtcwet - builds release version for RtCW: Enemy Territory
	@echo clean - cleans all output files and directories
	

rtcwet: $(BR)$(B_RTCWET)/$(BINARY).so
debug: $(BD)$(B_RTCWET)/$(BINARY).so

#return to castle wolfenstein: enemy territory
$(BR)$(B_RTCWET)/$(BINARY).so: $(BR)$(B_RTCWET) $(OBJR_RTCWET)
	$(CC) $(RELEASE_CFLAGS) $(SHLIBLDFLAGS) -o $@ $(OBJR_RTCWET)
  
$(BD)$(B_RTCWET)/$(BINARY).so: $(BD)$(B_RTCWET) $(OBJD_RTCWET)
	$(CC) $(DEBUG_CFLAGS) $(SHLIBLDFLAGS) -o $@ $(OBJD_RTCWET)


$(BR)$(B_RTCWET)/%.o: %.cpp $(HDR_FILES)
	$(CC) $(RELEASE_CFLAGS) $(RTCWET_FLAGS) $(SHLIBCFLAGS) -o $@ -c $<
  
$(BD)$(B_RTCWET)/%.o: %.cpp $(HDR_FILES)
	$(CC) $(DEBUG_CFLAGS) $(RTCWET_FLAGS) $(SHLIBCFLAGS) -o $@ -c $<
	
$(BR)$(B_RTCWET):
	@if [ ! -d $(BROOT) ];then mkdir $(BROOT);fi
	@if [ ! -d $(@) ];then mkdir $@;fi

$(BD)$(B_RTCWET):
	@if [ ! -d $(BROOT) ];then mkdir $(BROOT);fi
	@if [ ! -d $(@) ];then mkdir $@;fi
	
clean:
	@rm -rf $(BD)$(B_RTCWET) $(BR)$(B_RTCWET)
