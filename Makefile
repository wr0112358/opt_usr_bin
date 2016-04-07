default:
	@echo "no target specified. Try: make clean all && sudo make install"

CXXFLAGS = -Wall -Werror -O3 -std=c++1y $(shell pkg-config --cflags libaan) $(shell pkg-config --libs libaan)
#CXXFLAGS = -Wall -Werror -O0 -g -std=c++1y $(shell pkg-config --cflags libaan) $(shell pkg-config --libs libaan) -fsanitize=address -fno-omit-frame-pointer
LDLIBS=-lasan
color_regex: color_regex.cc
hex_search: hex_search.cc
open_shell_in_cwd_of: open_shell_in_cwd_of.cc
spidof: spidof.cc

h264_sprop_parameter_sets: CC=$(CXX)
h264_sprop_parameter_sets: LDLIBS+=$(shell pkg-config --libs gstreamer-plugins-bad-1.0 gstreamer-codecparsers-1.0 openssl)
h264_sprop_parameter_sets: CXXFLAGS+=-DGST_USE_UNSTABLE_API $(shell pkg-config --cflags gstreamer-plugins-bad-1.0)
h264_sprop_parameter_sets: h264_sprop_parameter_sets.o

clean:
	rm -f h264_sprop_parameter_sets open_shell_in_cwd_of spidof hex_search *.o

all: color_regex h264_sprop_parameter_sets hex_search open_shell_in_cwd_of spidof

install: all
	mkdir -p /opt/usr/bin
	cp crypto_mount /opt/usr/bin
	cp color_regex /opt/usr/bin
	cp open_shell_in_cwd_of /opt/usr/bin
	cp hex_search /opt/usr/bin
	cp spidof /opt/usr/bin
	cp png2pdf.sh /opt/usr/bin
	cp resize_win_at.sh /opt/usr/bin
	cp send_ip_on_change /opt/usr/bin
	cp ip_external.sh /opt/usr/bin
	cp rip_cd.sh /opt/usr/bin

test:
	make -C test/ test
