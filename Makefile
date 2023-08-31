## Haiku Generic Makefile v2.6 ##

## Fill in this file to specify the project being created, and the referenced
## Makefile-Engine will do all of the hard work for you. This handles any
## architecture of Haiku.
##
## For more information, see:
## file:///system/develop/documentation/makefile-engine.html

# The name of the binary.
NAME = Habitat

# The type of binary, must be one of:
#	APP:	Application
#	SHARED:	Shared library or add-on
#	STATIC:	Static library archive
#	DRIVER: Kernel driver
TYPE = APP

# If you plan to use localization, specify the application's MIME signature.
APP_MIME_SIG = application/x-vnd.habitat

#	The following lines tell Pe and Eddie where the SRCS, RDEFS, and RSRCS are
#	so that Pe and Eddie can fill them in for you.
#%{
# @src->@

#	Specify the source files to use. Full paths or paths relative to the
#	Makefile can be included. All files, regardless of directory, will have
#	their object files created in the common object directory. Note that this
#	means this Makefile will not work correctly if two source files with the
#	same name (source.c or source.cpp) are included from different directories.
#	Also note that spaces in folder names do not work well with this Makefile.
SRCS = \
	 src/Listener.cpp \
	 src/Lan.cpp  \
	 src/Base64.cpp  \
	 src/BJSON.cpp  \
	 src/Connection.cpp  \
	 src/Indices.cpp  \
	 src/JSON.cpp  \
	 src/Main.cpp  \
	 src/MUXRPC.cpp \
	 src/Post.cpp  \
	 src/Secret.cpp  \
	 src/SignJSON.cpp  \

TESTABLE_SRCS = src/JSON.cpp
TEST_SRCS = $(wildcard tests/*Spec.cpp)


#	Specify the resource definition files to use. Full or relative paths can be
#	used.
RDEFS = \
	 src/Habitat.rdef  \
	 src/icon.rdef  \


#	Specify the resource files to use. Full or relative paths can be used.
#	Both RDEFS and RSRCS can be utilized in the same Makefile.
RSRCS = \


# End Pe/Eddie support.
# @<-src@
#%}

#%}

#	Specify libraries to link against.
#	There are two acceptable forms of library specifications:
#	-	if your library follows the naming pattern of libXXX.so or libXXX.a,
#		you can simply specify XXX for the library. (e.g. the entry for
#		"libtracker.so" would be "tracker")
#
#	-	for GCC-independent linking of standard C++ libraries, you can use
#		$(STDCPPLIBS) instead of the raw "stdc++[.r4] [supc++]" library names.
#
#	- 	if your library does not follow the standard library naming scheme,
#		you need to specify the path to the library and it's name.
#		(e.g. for mylib.a, specify "mylib.a" or "path/mylib.a")
LIBS = be network bnetapi localestub sodium $(STDCPPLIBS) \
	icui18n icuuc icudata

#	Specify additional paths to directories following the standard libXXX.so
#	or libXXX.a naming scheme. You can specify full paths or paths relative
#	to the Makefile. The paths included are not parsed recursively, so
#	include all of the paths where libraries must be found. Directories where
#	source files were specified are	automatically included.
LIBPATHS =

#	Additional paths to look for system headers. These use the form
#	"#include <header>". Directories that contain the files in SRCS are
#	NOT auto-included here.
SYSTEM_INCLUDE_PATHS =

#	Additional paths paths to look for local headers. These use the form
#	#include "header". Directories that contain the files in SRCS are
#	automatically included.
LOCAL_INCLUDE_PATHS =

#	Specify the level of optimization that you want. Specify either NONE (O0),
#	SOME (O1), FULL (O3), or leave blank (for the default optimization level).
OPTIMIZE :=

# 	Specify the codes for languages you are going to support in this
# 	application. The default "en" one must be provided too. "make catkeys"
# 	will recreate only the "locales/en.catkeys" file. Use it as a template
# 	for creating catkeys for other languages. All localization files must be
# 	placed in the "locales" subdirectory.
LOCALES = en zh

#	Specify all the preprocessor symbols to be defined. The symbols will not
#	have their values set automatically; you must supply the value (if any) to
#	use. For example, setting DEFINES to "DEBUG=1" will cause the compiler
#	option "-DDEBUG=1" to be used. Setting DEFINES to "DEBUG" would pass
#	"-DDEBUG" on the compiler's command line.
DEFINES =

#	Specify the warning level. Either NONE (suppress all warnings),
#	ALL (enable all warnings), or leave blank (enable default warnings).
WARNINGS =

#	With image symbols, stack crawls in the debugger are meaningful.
#	If set to "TRUE", symbols will be created.
SYMBOLS :=

#	Includes debug information, which allows the binary to be debugged easily.
#	If set to "TRUE", debug info will be created.
DEBUGGER :=

#	Specify any additional compiler flags to be used.
COMPILER_FLAGS =

#	Specify any additional linker flags to be used.
LINKER_FLAGS =

#	Specify the version of this binary. Example:
#		-app 3 4 0 d 0 -short 340 -long "340 "`echo -n -e '\302\251'`"1999 GNU GPL"
#	This may also be specified in a resource.
APP_VERSION := 0.0.1

# Determine the CPU type
MACHINE=$(shell uname -m)
ifeq ($(MACHINE), BePC)
	CPU = x86
else
	CPU = $(MACHINE)
endif

# Set the core tools if they're not already specified.
MIMESET	:= mimeset
XRES	:= xres
RESCOMP	:= rc
CC		:= $(CC)
C++		:= $(CXX)

# Set up the linker & linker flags.
ifeq ($(origin LD), default)
	LD			:= $(CC)
endif
LDFLAGS += -Xlinker -soname=_APP_

OBJ_DIR := generated/objects
TARGET_DIR := generated/distro
TARGET := $(TARGET_DIR)/$(NAME)
TEST_DIR := generated/test
TEST_TARGET := $(TEST_DIR)/test-habitat

# Psuedo-function for converting a list of source files in SRCS variable to a
# corresponding list of object files in $(OBJ_DIR)/xxx.o. The "function" strips
# off the src file suffix (.ccp or .c or whatever) and then strips off the
# off the directory name, leaving just the root file name. It then appends the
# .o suffix and prepends the $(OBJ_DIR)/ path
define SRCS_LIST_TO_OBJS
	$(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(foreach file, $(SRCS), \
	$(basename $(notdir $(file))))))
endef

define TESTABLE_LIST_TO_OBJS
	$(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(foreach file, $(TESTABLE_SRCS), \
	$(basename $(notdir $(file))))))
endef

define TEST_LIST_TO_OBJS
	$(addprefix $(TEST_DIR)/, $(addsuffix .o, $(foreach file, $(TEST_SRCS), \
	$(basename $(notdir $(file))))))
endef

define SRCS_LIST_TO_DEPENDS
	$(addprefix $(OBJ_DIR)/, $(addsuffix .d, $(foreach file, $(SRCS), \
	$(basename $(notdir $(file))))))
endef

define TEST_LIST_TO_DEPENDS
	$(addprefix $(TEST_DIR)/, $(addsuffix .d, $(foreach file, $(TEST_SRCS), \
	$(basename $(notdir $(file))))))
endef

OBJS = $(SRCS_LIST_TO_OBJS)
TEST_OBJS = $(TEST_LIST_TO_OBJS) $(TESTABLE_LIST_TO_OBJS)
DEPENDS = $(SRCS_LIST_TO_DEPENDS) $(TEST_LIST_TO_DEPENDS)

# Create a unique list of paths to our sourcefiles and resources.
SRC_PATHS += $(sort $(foreach file, $(SRCS) $(RSRCS) $(RDEFS), $(dir $(file))))

# Add source paths to VPATH if not already present.
VPATH :=
VPATH += $(addprefix :, $(subst  ,:, $(filter-out $($(subst, :, ,$(VPATH))), $(SRC_PATHS))))

# Set up the local & system include paths and C++ stdlibs.
ifneq (,$(filter $(CPU),x86 x86_64))
	LOC_INCLUDES = $(foreach path, $(SRC_PATHS) $(LOCAL_INCLUDE_PATHS), $(addprefix -I, $(path)))
 ifeq ($(CC_VER), 2)
	INCLUDES = $(LOC_INCLUDES)
	INCLUDES += -I-
	INCLUDES += $(foreach path, $(SYSTEM_INCLUDE_PATHS), $(addprefix -I, $(path)))

	STDCPPLIBS = stdc++.r4
 else
	INCLUDES = -iquote./
	INCLUDES += $(foreach path, $(SRC_PATHS) $(LOCAL_INCLUDE_PATHS), $(addprefix -iquote, $(path)))
	INCLUDES += $(foreach path, $(SYSTEM_INCLUDE_PATHS), $(addprefix -isystem, $(path)))

	STDCPPLIBS = stdc++ supc++
 endif
else
ifeq ($(CPU), ppc)
	LOC_INCLUDES = $(foreach path, $(SRC_PATHS) $(LOCAL_INCLUDE_PATHS), $(addprefix -I, $(path)))
	SYS_INCLUDES += -i-
	SYS_INCLUDES += $(foreach path, $(SYSTEM_INCLUDE_PATHS), $(addprefix -i , $(path)))

	INCLUDES = $(LOC_INCLUDES) $(SYS_INCLUDES)
endif
endif
# Add the -L prefix to all of the library paths.
LINK_PATHS = $(foreach path, $(SRC_PATHS) $(LIBPATHS), \
	$(addprefix -L, $(path)))

# Handle the additional libraries specified. If the libraries have a .so or
# a .a prefix, or if they are _APP_ or _KERNEL_, simply add them to the list.
LINK_LIBS += $(filter %.so %.a _APP_ _KERNEL_, $(LIBS))
# If the libraries do not have suffixes and are not _APP_ or _KERNEL_,
# prepend -l to each name:(e.g. "be" becomes "-lbe").
LINK_LIBS += $(foreach lib, $(filter-out %.so %.a _APP_ _KERNEL_, $(LIBS)), $(addprefix -l, $(lib)))

# Add the linkpaths and linklibs to LDFLAGS.
LDFLAGS += $(LINK_PATHS)  $(LINK_LIBS)

# Add the defines to CFLAGS.
CFLAGS += $(foreach define, $(DEFINES), $(addprefix -D, $(define)))

# Add the additional compiler flags to CFLAGS.
CFLAGS += $(COMPILER_FLAGS)

# Add the additional linkflags to LDFLAGS
LDFLAGS += $(LINKER_FLAGS)

TEST_LDFLAGS := $(LINK_PATHS) $(LINK_LIBS) $(shell pkg-config --libs catch2) \
	-lCatch2Main -lCatch2

# Use the archiving tools to create an an archive if we're building a static
# library, otherwise use the linker.
ifeq ($(strip $(TYPE)), STATIC)
	BUILD_LINE = ar -cru "$(TARGET)" $(OBJS)
	TEST_BUILD_LINE = ar -cru "$(TEST_TARGET)" $(TEST_OBJS)
else
	BUILD_LINE = $(LD) -o "$@" $(OBJS) $(LDFLAGS)
	TEST_BUILD_LINE = $(LD) -o "$@" $(TEST_OBJS) $(TEST_LDFLAGS)
endif

# Pseudo-function for converting a list of resource definition files in RDEFS
# variable to a corresponding list of object files in $(OBJ_DIR)/xxx.rsrc.
# The "function" strips off the rdef file suffix (.rdef) and then strips
# of the directory name, leaving just the root file name. It then appends the
# the .rsrc suffix and prepends the $(OBJ_DIR)/ path.
define RDEFS_LIST_TO_RSRCS
	$(addprefix $(OBJ_DIR)/, $(addsuffix .rsrc, $(foreach file, $(RDEFS), \
	$(basename $(notdir $(file))))))
endef

# Create the resource definitions instruction in case RDEFS is not empty.
ifeq ($(RDEFS), )
	RSRCS +=
else
	RSRCS += $(RDEFS_LIST_TO_RSRCS)
endif

# Create the resource instruction.
ifeq ($(RSRCS), )
	DO_RSRCS :=
else
	DO_RSRCS := $(XRES) -o $(TARGET) $(RSRCS)
endif

# Set the directory for internationalization sources (catkeys) if it isn't
# already.
CATKEYS_DIR	:= locales

define LOCALES_LIST_TO_CATKEYS
	$(addprefix $(CATKEYS_DIR)/, $(addsuffix .catkeys, $(foreach lang, $(LOCALES), $(lang))))
endef
CATKEYS = $(LOCALES_LIST_TO_CATKEYS)

default: $(TARGET)

$(TARGET):	$(OBJ_DIR) $(TARGET_DIR) $(OBJS) $(RSRCS) $(CATKEYS)
	$(BUILD_LINE)
	$(DO_RSRCS)
	$(MIMESET) -f "$@"
	for lc in $(LOCALES); do linkcatkeys -o $(TARGET) -s $(APP_MIME_SIG) -tr -l $$lc $(CATKEYS_DIR)/$$lc.catkeys; done

$(TEST_TARGET): $(OBJ_DIR) $(TEST_DIR) $(TEST_OBJS)
	echo $(TEST_OBJS)
	$(TEST_BUILD_LINE)

# Create OBJ_DIR if it doesn't exist.
$(OBJ_DIR)::
	@[ -d $(OBJ_DIR) ] || mkdir $(OBJ_DIR) >/dev/null 2>&1

# Create TEST_DIR if it doesn't exist.
$(TEST_DIR)::
	@[ -d $(TEST_DIR) ] || mkdir $(TEST_DIR) >/dev/null 2>&1

$(TARGET_DIR)::
	@[ -d $(TARGET_DIR) ] || mkdir $(TARGET_DIR) >/dev/null 2>&1

# Create the localization sources directory if it doesn't exist.
$(CATKEYS_DIR)::
	@[ -d $(CATKEYS_DIR) ] || mkdir $(CATKEYS_DIR) >/dev/null 2>&1

# Rules to create the dependency files.
$(OBJ_DIR)/%.d : %.c
	mkdir -p $(OBJ_DIR); \
	mkdepend $(LOC_INCLUDES) -p .c:$(OBJ_DIR)/%n.o -m -f "$@" $<
$(OBJ_DIR)/%.d : %.cpp
	mkdir -p $(OBJ_DIR); \
	mkdepend $(LOC_INCLUDES) -p .cpp:$(OBJ_DIR)/%n.o -m -f "$@" $<
$(TEST_DIR)/%.d : tests/%.cpp
	mkdir -p $(TEST_DIR); \
	mkdepend $(LOC_INCLUDES) -p .cpp:$(TEST_DIR)/%n.o -m -f "$@" $<
$(OBJ_DIR)/%.d : %.cp
	mkdir -p $(OBJ_DIR); \
	mkdepend $(LOC_INCLUDES) -p .cp:$(OBJ_DIR)/%n.o -m -f "$@" $<
$(OBJ_DIR)/%.d : %.cc
	mkdir -p $(OBJ_DIR); \
	mkdepend $(LOC_INCLUDES) -p .cc:$(OBJ_DIR)/%n.o -m -f "$@" $<
$(OBJ_DIR)/%.d : %.cxx
	mkdir -p $(OBJ_DIR); \
	mkdepend $(LOC_INCLUDES) -p .cxx:$(OBJ_DIR)/%n.o -m -f "$@" $<
$(OBJ_DIR)/%.d : %.C
	mkdir -p $(OBJ_DIR); \
	mkdepend $(LOC_INCLUDES) -p .C:$(OBJ_DIR)/%n.o -m -f "$@" $<
$(OBJ_DIR)/%.d : %.CC
	mkdir -p $(OBJ_DIR); \
	mkdepend $(LOC_INCLUDES) -p .CC:$(OBJ_DIR)/%n.o -m -f "$@" $<
$(OBJ_DIR)/%.d : %.CPP
	mkdir -p $(OBJ_DIR); \
	mkdepend $(LOC_INCLUDES) -p .CPP:$(OBJ_DIR)/%n.o -m -f "$@" $<
$(TEST_DIR)/%.d : %.CPP
	mkdir -p $(OBJ_DIR); \
	mkdepend $(LOC_INCLUDES) -p .CPP:$(OBJ_DIR)/%n.o -m -f "$@" $<
$(OBJ_DIR)/%.d : %.CXX
	mkdir -p $(OBJ_DIR); \
	mkdepend $(LOC_INCLUDES) -p .CXX:$(OBJ_DIR)/%n.o -m -f "$@" $<

-include $(DEPENDS)
TEST_CFLAGS := $(shell pkg-config --cflags catch2)
# Rules to make the object files.
$(OBJ_DIR)/%.o : %.c
	$(CC) -c $< $(INCLUDES) $(CFLAGS) -o "$@"
$(OBJ_DIR)/%.o : %.cpp
	$(C++) -c $< $(INCLUDES) $(CFLAGS) -o "$@"
$(TEST_DIR)/%.o : tests/%.cpp
	$(C++) -c $< $(INCLUDES) $(CFLAGS) $(TEST_CFLAGS) -o "$@"
$(OBJ_DIR)/%.o : %.cp
	$(CC) -c $< $(INCLUDES) $(CFLAGS) -o "$@"
$(OBJ_DIR)/%.o : %.cc
	$(C++) -c $< $(INCLUDES) $(CFLAGS) -o "$@"
$(OBJ_DIR)/%.o : %.cxx
	$(C++) -c $< $(INCLUDES) $(CFLAGS) -o "$@"
$(OBJ_DIR)/%.o : %.C
	$(CC) -c $< $(INCLUDES) $(CFLAGS) -o "$@"
$(OBJ_DIR)/%.o : %.CC
	$(C++) -c $< $(INCLUDES) $(CFLAGS) -o "$@"
$(OBJ_DIR)/%.o : %.CPP
	$(C++) -c $< $(INCLUDES) $(CFLAGS) -o "$@"
$(TEST_DIR)/%.o : tests/%.CPP
	$(C++) -c $< $(INCLUDES) $(CFLAGS) $(TEST_CFLAGS) -o "$@"
$(OBJ_DIR)/%.o : %.CXX
	$(C++) -c $< $(INCLUDES) $(CFLAGS) -o "$@"

# Rules to compile the resource definition files.
$(OBJ_DIR)/%.rsrc : %.rdef
	cat $< | $(CC) -E $(INCLUDES) $(CFLAGS) - | grep -av '^#' | $(RESCOMP) -I $(dir $<) -o "$@" -
$(OBJ_DIR)/%.rsrc : %.RDEF
	cat $< | $(CC) -E $(INCLUDES) $(CFLAGS) - | grep -av '^#' | $(RESCOMP) -I $(dir $<) -o "$@" -

# Rule to preprocess program sources into file ready for collecting catkeys.
$(OBJ_DIR)/$(NAME).pre : $(SRCS)
	-cat $(SRCS) | $(CC) -E -x c++ $(INCLUDES) $(CFLAGS) -DB_COLLECTING_CATKEYS - | grep -av '^#' > $(OBJ_DIR)/$(NAME).pre

$(CATKEYS_DIR)/en.catkeys : $(CATKEYS_DIR) $(OBJ_DIR)/$(NAME).pre
	collectcatkeys -s $(APP_MIME_SIG) $(OBJ_DIR)/$(NAME).pre -o $(CATKEYS_DIR)/en.catkeys

.PHONY: test
test: $(TEST_TARGET)
	./$(TEST_TARGET)

# The generic "clean" command. (Deletes everything in the object folder.)
.PHONY: clean
clean:
	-rm -rf "$(OBJ_DIR)"

# Remove just the application from the object folder.
.PHONY: rmapp
rmapp ::
	-rm -f $(TARGET)

