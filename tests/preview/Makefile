# Renders UI screenshots on the host: make -C tests/preview
# Writes build/*.ppm; convert them to PNG with any image tool.
CC      ?= gcc
SRC     := ../../source
OUT     := build
CFLAGS  := -std=gnu11 -O1 -g -Wall -Wextra -I$(SRC) -I../../third_party -I$(OUT)
UI_SRC  := gfx.c audioformat.c font.c ui.c ui_now.c ui_nowstyle.c ui_cassette.c background.c ui_common.c ui_list.c ui_playlists.c ui_overlay.c ui_buttons.c ui_library.c library_tab.c catalog.c ui_home.c home.c history.c \
           keyboard.c menu.c settings.c ui_hit.c theme.c eq.c util.c cover.c image_impl.c lang.c

.PHONY: run clean

# Every language in data/ too, each in build/<id>: what no longer fits shows.
# For Japanese, Chinese and Korean, KAIPLAYER_CJK_FONT names a font with them
# (font_system_host.c); without one, their text draws as boxes.
LANGS   := $(patsubst ../../data/lang_%.txt,%,$(wildcard ../../data/lang_*.txt))

run: $(OUT)/preview
	./$(OUT)/preview $(OUT) assets/track.flac
	@for l in $(LANGS); do mkdir -p $(OUT)/$$l && KAI_LANG_FILE=../../data/lang_$$l.txt ./$(OUT)/preview $(OUT)/$$l assets/track.flac || exit 1; done

$(OUT)/font_regular_ttf.c: ../../data/font_regular.ttf embed.py
	python3 embed.py $< $(OUT) font_regular_ttf

$(OUT)/font_bold_ttf.c: ../../data/font_bold.ttf embed.py
	python3 embed.py $< $(OUT) font_bold_ttf

$(OUT)/preview: preview.c font_system_host.c $(addprefix $(SRC)/,$(UI_SRC)) $(OUT)/font_regular_ttf.c $(OUT)/font_bold_ttf.c
	$(CC) $(CFLAGS) preview.c font_system_host.c $(addprefix $(SRC)/,$(UI_SRC)) \
		$(OUT)/font_regular_ttf.c $(OUT)/font_bold_ttf.c -o $@ -lm

clean:
	rm -rf $(OUT)
