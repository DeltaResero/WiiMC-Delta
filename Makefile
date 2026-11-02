#---------------------------------------------------------------------------------
# Clear the implicit built in rules
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/wii_rules
export	FREETYPE_CFLAGS 	:=	`$(DEVKITPRO)/portlibs/ppc/bin/powerpc-eabi-pkg-config --cflags freetype2`
export	FREETYPE_LIBS	:=	`$(DEVKITPRO)/portlibs/ppc/bin/powerpc-eabi-pkg-config --libs freetype2`

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing extra header files
#---------------------------------------------------------------------------------
MPLAYER		:=	$(CURDIR)/src/mplayer
TARGET		:=	wiimc
TARGETDIR	:=	executables
BUILD		:=	build
SOURCES		:=	src src/libwiigui src/utils src/utils/unzip
DATA		:=	src/images src/lang src/fonts
INCLUDES	:=	src src/mplayer

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------

# Compile settings, NTFS = 191KB, EXT2 = 91KB, Full = 282KB
ENABLE_NTFS                   = 1
ENABLE_EXT2                   = 0

CFLAGS		=	-g -O3 -Wall $(MACHDEP) $(INCLUDE) $(FREETYPE_CFLAGS) \
				-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -Wframe-larger-than=8192
CXXFLAGS	=	$(CFLAGS)
LDFLAGS		=	-g $(MACHDEP) -Wl,-Map,$(notdir $@).map
LDFLAGS		+=	-L../buildtools

#---------------------------------------------------------------------------------
# any extra libraries we wish to link with the project
#---------------------------------------------------------------------------------
LIBS    := -lmplayerwii -lavformat -lavcodec -lswscale -lavutil \
                        -ljpeg -ldi -liso9660 -liconv -lpng -lz -lmxml $(FREETYPE_LIBS) \
                        -lfat -lwiiuse -lbte -logc -ltinysmb

ifeq ($(ENABLE_NTFS), 1)
CFLAGS += -DWANT_NTFS
LIBS += -lntfs
endif

ifeq ($(ENABLE_EXT2), 1)
CFLAGS += -DWANT_EXT2
LIBS += -lext2fs
endif

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS)


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGETDIR)/$(TARGET)

#---------------------------------------------------------------------------------
# vendored libraries (src/lib/) are built into a prefix inside the build directory
#---------------------------------------------------------------------------------
LIBSRC		:=	$(CURDIR)/src/lib
EXTBUILD	:=	$(CURDIR)/$(BUILD)/external-build
export EXTPREFIX :=	$(CURDIR)/$(BUILD)/external
EXTLIBS		:=	$(EXTPREFIX)/lib/libntfs.a $(EXTPREFIX)/lib/libiconv.a
# libraries built in this tree, so the DOL relinks when one of them changes
export LINKDEPS	:=	$(MPLAYER)/libmplayerwii.a $(MPLAYER)/ffmpeg/libavformat/libavformat.a \
				$(MPLAYER)/ffmpeg/libavcodec/libavcodec.a $(MPLAYER)/ffmpeg/libswscale/libswscale.a \
				$(MPLAYER)/ffmpeg/libavutil/libavutil.a $(EXTLIBS)
EXTCONFIGURE	:=	--quiet --host=powerpc-eabi --prefix=$(EXTPREFIX) --disable-shared \
				--enable-static --disable-nls CFLAGS="-O2 $(MACHDEP) -fno-asynchronous-unwind-tables"
# FFmpeg 9.0.2 (src/lib/ffmpeg) is configured and built out of tree, with
# the options in src/lib/ffmpeg-wii.mak. It installs into a prefix of its
# own, since in $(EXTPREFIX) its libraries and headers would come before
# FFmpeg 0.10's.
FFPREFIX	:=	$(CURDIR)/$(BUILD)/ffmpeg
include $(LIBSRC)/ffmpeg-wii.mak
FFSRC		:=	$(LIBSRC)/ffmpeg
FFBUILD		:=	$(EXTBUILD)/ffmpeg
FFMPEGLIBS	:=	$(foreach lib,avformat avcodec swscale swresample avutil,$(FFPREFIX)/lib/lib$(lib).a)
FFMPEGFILES	:=	$(wildcard $(addprefix $(FFSRC)/,lib*/*.[chS] lib*/*/*.[chS] lib*/*/*/*.[chS]))
NTFSOBJS	:=	$(patsubst %.c,$(EXTBUILD)/libntfs/%.o,$(notdir $(wildcard src/lib/libntfs/source/*.c)))

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

#---------------------------------------------------------------------------------
# automatically build a list of object files for our project
#---------------------------------------------------------------------------------
CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
sFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))
# language files are embedded gzipped (gettext.cpp unpacks the chosen one)
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.ttf) \
					$(addsuffix .gz,$(wildcard $(dir)/*.lang)) $(wildcard $(dir)/*.png) \
					$(wildcard $(dir)/*.jpg)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(sFILES:.s=.o) $(SFILES:.S=.o)
export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES := $(addsuffix .h,$(subst .,_,$(BINFILES)))

#---------------------------------------------------------------------------------
# build a list of include paths
#---------------------------------------------------------------------------------
export INCLUDE	:=	$(foreach dir,$(INCLUDES), -iquote $(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD) \
					-I$(EXTPREFIX)/include \
					-I$(LIBOGC_INC)

#---------------------------------------------------------------------------------
# build a list of library paths
#---------------------------------------------------------------------------------

export LIBPATHS	:= -L$(LIBOGC_LIB) $(foreach dir,$(LIBDIRS),-L$(dir)/lib) \
				-L$(EXTPREFIX)/lib \
				-L$(MPLAYER)/ \
				-L$(MPLAYER)/ffmpeg/libavcodec \
				-L$(MPLAYER)/ffmpeg/libavformat \
				-L$(MPLAYER)/ffmpeg/libavutil \
				-L$(MPLAYER)/ffmpeg/libswscale

export OUTPUT	:=	$(CURDIR)/$(TARGETDIR)/$(TARGET)
.PHONY: $(BUILD) clean forwarder

#---------------------------------------------------------------------------------
$(BUILD): $(EXTLIBS) $(FFMPEGLIBS)
	@[ -d $(TARGETDIR) ] || mkdir -p $(TARGETDIR)
	cd src/mplayer; $(MAKE) -f Makefile; cd ../..
	@[ -d $@ ] || mkdir -p $@
	@make --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
$(EXTBUILD)/libntfs/%.o: $(LIBSRC)/libntfs/source/%.c
	@mkdir -p $(@D)
	$(CC) -O2 -Wall -Wno-address-of-packed-member -fno-asynchronous-unwind-tables \
		$(MACHDEP) -DHAVE_CONFIG_H -I$(LIBSRC)/libntfs/include -I$(LIBOGC_INC) -c $< -o $@

$(EXTPREFIX)/lib/libntfs.a: $(NTFSOBJS)
	@mkdir -p $(@D) $(EXTPREFIX)/include
	rm -f $@
	$(AR) -rc $@ $^
	cp $(LIBSRC)/libntfs/include/ntfs.h $(EXTPREFIX)/include/

# libiconv: only libcharset and lib/ are built
$(EXTPREFIX)/lib/libiconv.a:
	@rm -rf $(EXTBUILD)/libiconv; mkdir -p $(EXTBUILD)/libiconv
	cd $(EXTBUILD)/libiconv && $(LIBSRC)/libiconv/configure $(EXTCONFIGURE)
	$(MAKE) -C $(EXTBUILD)/libiconv lib/localcharset.h
	$(MAKE) -C $(EXTBUILD)/libiconv/lib
	$(MAKE) -C $(EXTBUILD)/libiconv/lib install-lib libdir=$(EXTPREFIX)/lib includedir=$(EXTPREFIX)/include
	cp $(EXTBUILD)/libiconv/include/iconv.h.inst $(EXTPREFIX)/include/iconv.h

# FFmpeg 9: reconfigured from scratch when its options change, rebuilt and
# reinstalled when any of its sources change. VPATH is WiiMC's, not FFmpeg's.
$(FFBUILD)/ffbuild/config.mak: $(LIBSRC)/ffmpeg-wii.mak $(FFSRC)/configure
	@rm -rf $(FFBUILD); mkdir -p $(FFBUILD)
	cd $(FFBUILD) && $(FFSRC)/configure $(FFMPEG_CONFIGURE) > configure.log || { tail -30 configure.log; false; }

$(firstword $(FFMPEGLIBS)): $(FFBUILD)/ffbuild/config.mak $(FFMPEGFILES)
	$(MAKE) -C $(FFBUILD) VPATH=
	$(MAKE) -C $(FFBUILD) VPATH= install

$(wordlist 2,$(words $(FFMPEGLIBS)),$(FFMPEGLIBS)): $(firstword $(FFMPEGLIBS))
	@:

#---------------------------------------------------------------------------------
# the channel forwarder in extras/, which links the libntfs built here
forwarder: $(EXTPREFIX)/lib/libntfs.a
	$(MAKE) -C extras/forwarder

#---------------------------------------------------------------------------------
test:
	@make --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile


clean:
	@echo clean ...
	rm -f $(BUILD)/*.d $(BUILD)/*.h $(BUILD)/*.ii $(BUILD)/*.lst $(BUILD)/*.map \
	$(BUILD)/*.o $(BUILD)/*.s
	@rm -fr $(OUTPUT).elf $(OUTPUT).dol
	rm -rf $(EXTPREFIX) $(EXTBUILD) $(FFPREFIX)
	cd src/mplayer; $(MAKE) -f Makefile clean
	$(MAKE) -C extras/forwarder clean

#---------------------------------------------------------------------------------
run:
	wiiload $(OUTPUT).dol

#---------------------------------------------------------------------------------
reload:
	wiiload -r $(OUTPUT).dol

#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).dol: $(OUTPUT).elf
# not devkitPro's %.elf rule: it links $^, which would put LINKDEPS ahead of LIBS
$(OUTPUT).elf: $(OFILES) $(LINKDEPS)
	$(SILENTMSG) linking ... $(notdir $@)
	$(ADD_COMPILE_COMMAND) end
	$(SILENTCMD)$(LD) $(OFILES) $(LDFLAGS) $(LIBPATHS) $(LIBS) -o $@

# the .d files name these headers by absolute path, which make doesn't tie to
# the bin2o rules below, so a changed file's size would never reach the code
$(OFILES_SOURCES) : $(HFILES)

#---------------------------------------------------------------------------------
# This rule links in binary data with .ttf, .lang, .png, and .jpg extensions
#---------------------------------------------------------------------------------
%.ttf.o %_ttf.h : %.ttf
	@echo $(notdir $<)
	$(bin2o)

# kept, or make would rebuild them, and everything after, on every run
.PRECIOUS: %.lang.gz

%.lang.gz : %.lang
	@echo $(notdir $<)
	@gzip -9 -n -c $< > $@

%.lang.gz.o %_lang_gz.h : %.lang.gz
	$(bin2o)

%.png.o %_png.h : %.png
	@echo $(notdir $<)
	$(bin2o)

%.jpg.o %_jpg.h : %.jpg
	@echo $(notdir $<)
	$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
