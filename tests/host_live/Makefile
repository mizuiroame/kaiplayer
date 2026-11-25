# The network folder against real servers, on a PC, with sanitizers. In the
# image this folder's Dockerfile makes (kaiplayer-host-live), from the repository:
#   MSYS_NO_PATHCONV=1 docker run --rm --security-opt seccomp=unconfined \
#     --add-host=host.docker.internal:host-gateway -v "$PWD:/w" -v "D:/music:/music:ro" \
#     -e SMB_ADDRESS='\\host.docker.internal:4451\Music' -e SMB_USER=... -e SMB_PASS=... -e MUSIC=/music \
#     -w /w/tests/host_live kaiplayer-host-live make smb
# MUSIC is the share's songs on this computer, to compare with what comes over the network.
CC      ?= gcc
SRC     := ../../source
SAN     := -fsanitize=address,undefined -fno-omit-frame-pointer
FFMPEG  ?= /opt/ffmpeg-host
CFLAGS  := -std=gnu11 -g -O1 -Wall -Wextra -fwrapv -I$(SRC) -I../../third_party -isystem $(FFMPEG)/include $(SAN) \
           -DKAIPLAYER_VERSION=\"host\"
LDFLAGS := $(SAN) -L$(FFMPEG)/lib -lavformat -lavcodec -lswresample -lavutil -lsmb2 -lcurl -lm -lpthread
OBJDIR  := build
NOASLR  := setarch $$(uname -m) -R

NETWORK := dav.c md4.c davnet.c smbnet.c net.c davfile.c kfile.c blockcache.c util.c lang.c tracklist.c library.c \
           decoder.c dsd.c ffaudio.c audioformat.c tags_file.c tags_id3.c tags_vorbis.c tags_text.c cover.c \
           catalog.c md5.c json.c subsonic.c subnet.c subfile.c

.PHONY: smb navidrome clean

smb: $(OBJDIR)/host_smb
	$(NOASLR) ./$(OBJDIR)/host_smb

$(OBJDIR)/host_smb: host_smb.c $(addprefix $(SRC)/,$(NETWORK)) $(OBJDIR)/codecs_impl.o $(OBJDIR)/image_impl.o
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) host_smb.c $(addprefix $(SRC)/,$(NETWORK)) $(OBJDIR)/codecs_impl.o $(OBJDIR)/image_impl.o \
		-o $@ $(LDFLAGS)

# The Navidrome server, the same way:
#   ... -e NAVIDROME=http://host.docker.internal:4533 -e NAVIDROME_USER=... -e NAVIDROME_PASS=... -e MUSIC=/music
#   ... kaiplayer-host-live make navidrome
# NAVIDROME_UNDER_PATH (optional): another Navidrome with ND_BASEURL=/navidrome, its address without the path.
navidrome: $(OBJDIR)/host_navidrome
	$(NOASLR) ./$(OBJDIR)/host_navidrome

$(OBJDIR)/host_navidrome: host_navidrome.c $(addprefix $(SRC)/,$(NETWORK)) $(OBJDIR)/codecs_impl.o $(OBJDIR)/image_impl.o
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) host_navidrome.c $(addprefix $(SRC)/,$(NETWORK)) $(OBJDIR)/codecs_impl.o $(OBJDIR)/image_impl.o \
		-o $@ $(LDFLAGS)

$(OBJDIR)/%_impl.o: $(SRC)/%_impl.c
	@mkdir -p $(OBJDIR)
	$(CC) -O2 -I$(SRC) -I../../third_party -c $< -o $@

clean:
	rm -rf $(OBJDIR)
