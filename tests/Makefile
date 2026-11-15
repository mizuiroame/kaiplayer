# Host build of the tests, with sanitizers and coverage. Run: make -C tests
CC      ?= gcc
SRC     := ../source
CFLAGS  := -std=gnu11 -g -O1 -Wall -Wextra -I$(SRC) -I../third_party \
           -fsanitize=address,undefined -fno-omit-frame-pointer --coverage
LDFLAGS := -fsanitize=address,undefined --coverage -lm

TESTED  := util.c queue.c resampler.c decoder.c library.c browser.c
OBJDIR  := build

.PHONY: run coverage clean

# GCC 12's ASan can hang on kernels with high mmap entropy, so ASLR is off.
# In Docker this needs --security-opt seccomp=unconfined.
run: $(OBJDIR)/test_main
	setarch $$(uname -m) -R ./$(OBJDIR)/test_main

coverage: run
	cd $(OBJDIR) && gcov -n *.gcda 2>/dev/null | grep -A1 "File '.*source/" | grep -v "^--"

$(OBJDIR)/test_main: test_main.c $(addprefix $(SRC)/,$(TESTED)) $(OBJDIR)/codecs_impl.o
	@mkdir -p $(OBJDIR)
	cd $(OBJDIR) && $(CC) $(CFLAGS) -I../$(SRC) -I../../third_party ../test_main.c \
		$(addprefix ../$(SRC)/,$(TESTED)) codecs_impl.o -o test_main $(LDFLAGS)

# The decoder libraries are built without instrumentation; they are not ours.
$(OBJDIR)/codecs_impl.o: $(SRC)/codecs_impl.c
	@mkdir -p $(OBJDIR)
	$(CC) -O2 -I$(SRC) -I../third_party -c $< -o $@

clean:
	rm -rf $(OBJDIR)
