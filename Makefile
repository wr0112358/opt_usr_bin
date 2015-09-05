default:
	@echo "no target specified."

CXXFLAGS = -Wall -Werror -O3 -std=c++1y $(shell pkg-config --cflags libaan) $(shell pkg-config --libs libaan)

color_regex: color_regex.cc
hex_search: hex_search.cc
open_shell_in_cwd_of: open_shell_in_cwd_of.cc
spidof: spidof.cc

clean:
	rm -f open_shell_in_cwd_of spidof hex_search *.o

all: color_regex hex_search open_shell_in_cwd_of spidof

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
