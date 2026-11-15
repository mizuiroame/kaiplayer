# Host builds of the tests and the fuzzer, with sanitizers.
#   make -C tests run        unit and file tests
#   make -C tests coverage   the same, then line coverage per file
#   make -C tests fuzz       mutation fuzzing of every file parser
CC      ?= gcc
SRC     := ../source
SAN     := -fsanitize=address,undefined -fno-omit-frame-pointer
# -fwrapv as in the .nro: corrupt FLAC data overflows dr_flac's LPC sums.
CFLAGS  := -std=gnu11 -g -O1 -Wall -Wextra -fwrapv -I$(SRC) -I../third_party $(SAN)
LDFLAGS := $(SAN) -lm
OBJDIR  := build

TESTED  := util.c queue.c resampler.c decoder.c library.c browser.c \
           tags_text.c tags_id3.c tags_vorbis.c tags_file.c cover.c \
           tracklist.c playlist.c resume.c keyboard.c menu.c
TESTS   := test_main.c test_tags.c test_cover.c test_playlist.c
PARSERS := util.c decoder.c tags_text.c tags_id3.c tags_vorbis.c tags_file.c cover.c \
           codecs_impl.c image_impl.c
ITER    ?= 1000

# GCC 12's ASan can hang on kernels with high mmap entropy, so ASLR is off.
# In Docker this needs --security-opt seccomp=unconfined.
NOASLR  := setarch $$(uname -m) -R

.PHONY: run coverage fuzz tagdump clean

run: $(OBJDIR)/test_main
	$(NOASLR) ./$(OBJDIR)/test_main

coverage: run
	cd $(OBJDIR) && gcov -n *.gcda 2>/dev/null | grep -A1 "File '.*source/" | grep -v "^--"

fuzz: $(OBJDIR)/fuzz
	$(NOASLR) ./$(OBJDIR)/fuzz $(ITER)

$(OBJDIR)/test_main: $(TESTS) check.h $(addprefix $(SRC)/,$(TESTED)) $(OBJDIR)/codecs_impl.o $(OBJDIR)/image_impl.o
	@mkdir -p $(OBJDIR)
	cd $(OBJDIR) && $(CC) $(CFLAGS) --coverage -I../$(SRC) -I../../third_party \
		$(addprefix ../,$(TESTS)) $(addprefix ../$(SRC)/,$(TESTED)) codecs_impl.o image_impl.o \
		-o test_main $(LDFLAGS) --coverage

# For the unit tests the third-party libraries are built without
# instrumentation; the fuzzer builds them with it.
$(OBJDIR)/%_impl.o: $(SRC)/%_impl.c
	@mkdir -p $(OBJDIR)
	$(CC) -O2 -I$(SRC) -I../third_party -c $< -o $@

$(OBJDIR)/fuzz: fuzz.c $(addprefix $(SRC)/,$(PARSERS))
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -O2 fuzz.c $(addprefix $(SRC)/,$(PARSERS)) -o $@ $(LDFLAGS)

# make -C tests tagdump; then build/tagdump FILE... shows what KaiPlayer reads.
tagdump: $(OBJDIR)/tagdump

$(OBJDIR)/tagdump: tagdump.c $(addprefix $(SRC)/,$(PARSERS))
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -O2 tagdump.c $(addprefix $(SRC)/,$(PARSERS)) -o $@ $(LDFLAGS)

clean:
	rm -rf $(OBJDIR)
