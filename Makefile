CC = clang
CFLAGS = -std=c99 -Wall -Wextra -Werror -O2 -W -DSDS_ABORT_ON_OOM -g
# CFLAGS = -std=c99 -Wall -Wextra -Werror -g -O2 -fsanitize=address
LDFLAGS = -lreadline
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

# Debug flags - empty by default
DEBUG_FLAGS =

# Create directories if they don't exist
$(shell mkdir -p $(OBJ_DIR) $(BIN_DIR))

# Source files
SRCS = $(wildcard $(SRC_DIR)/*.c)
# Object files (replace src/ with obj/ in the path)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
# Main executable
MAIN = $(BIN_DIR)/ditto

.PHONY: all clean debug release kill

all: $(MAIN)


# Debug build with all debug flags enabled
debug: DEBUG_FLAGS += -DDITTO_DEBUG_ALL
debug: CFLAGS += $(DEBUG_FLAGS)
debug: $(MAIN)

# Release build (no debug flags)
release: $(MAIN)

# Linking rule
$(MAIN): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Compilation rule: each .c file to corresponding .o file
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Include automatically generated dependencies
-include $(OBJS:.o=.d)

# Rule to generate a dependency file
$(OBJ_DIR)/%.d: $(SRC_DIR)/%.c
	@set -e; rm -f $@; \
	$(CC) -MM $(CFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,$(OBJ_DIR)/\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

clean:
	rm -rf $(OBJ_DIR)/* $(BIN_DIR)/* *.o *.d

# Run the program in REPL mode
run: $(MAIN)
	$(MAIN)

# Test targets
PHONY: test-dmalloc
test-dmalloc:
	$(CC) -DTESTS_DMALLOC -o bin/dmalloc-test src/dmalloc.c && bin/dmalloc-test

PHONY: test-fss
test-fss:
	$(CC) -DTESTS_FSS -o bin/fss-test src/fss.c src/dmalloc.c && bin/fss-test

# Unstuck process while developing if editor gets blocked
kill:
	scripts/kill.sh

# Debug with GDB
gdb-debug: debug
	gdb $(MAIN)
