
	STRIP ?= strip

	# check on which OS we build
	UNAME_S := $(shell uname -s)
	# check which architecture we build on
	UNAME_M := $(shell uname -m)
	# check which architecture we build for
	TARGET_ARCH = $(shell $(CXX) -dumpmachine | sed 's/-.*//')

ifneq ($(UNAME_M),$(TARGET_ARCH))
  CROSS_COMPILING = 1
  $(info $(yellow) INFO: $(reset)Cross Compile $(blue)$(UNAME_M)$(reset) to $(blue)$(TARGET_ARCH)$(reset))
endif

# avoid optimisation for x86_64 arch when we cross compile
ifneq ($(CROSS_COMPILING), 1)

# get flags supported by CPU
ifeq ($(UNAME_S), FreeBSD)
  CPU_INFO = dmesg | grep Features | tr A-Z a-z
  ifeq ($(UNAME_M), amd64)
    UNAME_M = x86_64
  endif
else
  CPU_INFO = cat /proc/cpuinfo | grep flags
endif

# check for sse optimisation level only on x86_64 architecture
ifeq ($(TARGET_ARCH), x86_64)
  ifneq ($$(filter $(CPU_INFO) | grep sse3 ) , )
    SSE_CFLAGS = -msse3 -mfpmath=sse -mfxsr -DUSE_SSE=1
    FFT_FLAG = -DFFTCONVOLVER_USE_SSE=1
  else ifneq ($$(filter $(CPU_INFO) | grep sse2 ) , )
    SSE_CFLAGS = -msse2 -mfpmath=sse -mfxsr -DUSE_SSE=1
    FFT_FLAG = -DFFTCONVOLVER_USE_SSE=1
  else ifneq ($$(filter $(CPU_INFO) | grep sse ) , )
    SSE_CFLAGS = -msse -mfpmath=sse -mfxsr -DUSE_SSE=1
    FFT_FLAG = -DFFTCONVOLVER_USE_SSE=1
  else ifneq ($$(filter $(CPU_INFO) | grep ARM ) , )
    ifneq ($$(filter $(CPU_INFO) | grep ARMv7 ) , )
      ifneq ($$(filter $(CPU_INFO) | grep vfpd32 ) , )
        SSE_CFLAGS = -march=armv7-a -mfpu=vfpv3 
      else ifneq ($$(filter $(CPU_INFO) | grep vfpv3 ) , )
        SSE_CFLAGS = -march=armv7-a -mfpu=vfpv3
      endif
    else
      ARMCPU = "YES"
    endif
  endif
else
  SSE_CFLAGS =
endif

endif


ifeq ($(MAKECMDGOALS),debug)
  CXXFLAGS += -g -DDEBUG
else
  CXXFLAGS += -O3 -DNDEBUG
endif

	# set bundle name
	NAME = smoothir
	VER = 0.1

	PREFIX ?= /usr
	# check if user is root
	user = $(shell whoami)
	ifeq ($(user),root)
	INSTALL_DIR ?= $(PREFIX)/lib/lv2
	EXE_INSTALL_DIR ?= $(PREFIX)/bin
	else 
	INSTALL_DIR ?= ~/.lv2
	EXE_INSTALL_DIR ?= ~/bin
	endif

	SHARE_DIR ?= $(PREFIX)/share
	DESKAPPS_DIR ?= $(SHARE_DIR)/applications
	PIXMAPS_DIR ?= $(SHARE_DIR)/pixmaps
	PKGCONFIG = pkg-config

	INCLUDES := -I..
	LIB_DIR := ../libxputty/libxputty/
	HEADER_DIR := $(LIB_DIR)include/
	UI_LIB:= $(LIB_DIR)libxputty.$(STATIC_LIB_EXT)


	# set compile flags
	GUI_LDFLAGS += -I$(HEADER_DIR) -Wl,-Bstatic -L. $(UI_LIB) \
	-Wl,-Bdynamic `pkg-config --cflags --libs cairo x11` 

	CFLAGS += -Wall -funroll-loops `$(PKGCONFIG) $(PKGCONFIG_FLAGS) --cflags sndfile fftw3 `\
	-ffast-math -fomit-frame-pointer -fstrength-reduce -fdata-sections \
	-pthread $(SSE_CFLAGS)

	CXXFLAGS += -MMD -std=c++17 -fno-math-errno -fno-trapping-math -fPIC -D_OS_UNIX_ -DALVER=\"$(VER)\" $(CFLAGS)

	LDFLAGS += $(INCLUDES) -lm -pthread -lpthread -lstdc++ $(GUI_LDFLAGS) \
	`$(PKGCONFIG) $(PKGCONFIG_FLAGS) --libs sndfile fftw3 `
	ifneq ($(MACOS)$(WINDOWS),true)
		LDFLAGS += -lrt -lc
	endif

	ifneq (,$(findstring clang, $(CXX)))
		CXXFLAGS := $(filter-out -fstrength-reduce,$(CXXFLAGS))
		CXXFLAGS += -Wno-unused-private-field
	endif

	# invoke build files
	OBJECTS = main.cpp

	DEPS = smoothir.d

.PHONY : all 


all : check $(NAME)
	$(QUIET)mkdir -p ../bin
	$(QUIET)cp ./$(NAME)$(EXE) ../bin
	@if ! [ -f ../bin/$(NAME)$(EXE) ]; then \
		$(R_ECHO) "Sorry, build fail$(reset)"; \
	fi

debug : all


-include $(DEPS)

check :
ifdef ARMCPU
	@$(R_ECHO)"ARM CPU DEDECTED, please check the optimization flags$(reset)"
endif

info:
	$(info $(yellow) INFO: $(reset) cxxflags $(blue) $(CXX) $(CXXFLAGS) $(reset))
	$(info $(yellow) INFO: $(reset) ldflags  $(blue) $(LD) $(LDFLAGS) $(GUI_LDFLAGS))


clean :
	$(QUIET)rm -f *.o *.d *.a *.lib 
	$(QUIET)rm -f $(NAME) $(NAME).exe 
	$(QUIET)rm -rf ../bin

dist-clean :

install :
ifneq ("$(wildcard ../bin/$(NAME)$(EXE))","")
	$(QUIET)mkdir -p $(DESTDIR)$(EXE_INSTALL_DIR)
	$(QUIET)cp ./$(BUILD_DIR)/$(NAME) $(DESTDIR)$(EXE_INSTALL_DIR)/$(NAME)
  ifeq ($(user),root)
	$(QUIET)mkdir -p $(DESTDIR)$(DESKAPPS_DIR)
	$(QUIET)cp $(NAME).desktop $(DESTDIR)$(DESKAPPS_DIR)/$(NAME).desktop
	$(QUIET)cp $(NAME).svg $(DESTDIR)$(PIXMAPS_DIR)/$(NAME).svg
	$(QUIET)update-desktop-database || true
  endif
else
	@$(B_ECHO) "$(NAME)$(EXE) Standalone skipped$(reset)"
endif

uninstall :
	$(QUIET)rm -rf $(DESTDIR)$(EXE_INSTALL_DIR)/$(NAME)

$(NAME) : $(OBJECTS)
	$(CXX) -MMD $(CXXFLAGS) $(OBJECTS) -o $(NAME)$(EXE) $(LDFLAGS)
ifneq ($(MAKECMDGOALS),debug)
	$(STRIP) -s -x -X -R .note.ABI-tag $(NAME)$(EXE)
endif

doc:
	#pass
