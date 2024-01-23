SRCS = nhops.cpp

TARGET := $(PLUGINS_TARGET)/nhops.so
OBJ_DIR := objects

.PHONY: default
default: $(TARGET)

LOC_INCLUDES = -I. -I../..
INCLUDES = $(LOC_INCLUDES)

CFLAGS = -gdwarf-3 -Wall

define SRCS_LIST_TO_OBJS
	$(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(foreach file, $(SRCS), \
	$(basename $(notdir $(file))))))
endef

$(OBJ_DIR)/%.d : %.cpp
	mkdir -p $(OBJ_DIR); \
	mkdepend $(LOC_INCLUDES) -p .cpp:$(OBJ_DIR)/%n.o -m -f "$@" $<

$(OBJ_DIR)/%.o : %.cpp
	$(CC) -c $< $(INCLUDES) $(CFLAGS) -o "$@"

define SRCS_LIST_TO_DEPENDS
	$(addprefix $(OBJ_DIR)/, $(addsuffix .d, $(foreach file, $(SRCS) $(LIB_SRCS), \
	$(basename $(notdir $(file))))))
endef

OBJS = $(SRCS_LIST_TO_OBJS)
DEPENDS = $(SRCS_LIST_TO_DEPENDS)
-include $(DEPENDS)

LDFLAGS := -L$(TARGET_DIR) -lbe -lhabitat

$(TARGET): $(OBJS)
	$(LD) -shared -o "$@" $(OBJS) $(LDFLAGS)

.PHONY: clean
clean:
	-rm -rf $(OBJ_DIR)
