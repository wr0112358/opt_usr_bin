default:
	@echo "no target specified."

open_shell:
	g++ -std=c++0x -Wall -Werror open_shell_in_cwd_of.cc -lX11 -lXmu -o open_shell_in_cwd_of -I/opt/usr/include

clean:
	rm -f open_shell_in_cwd_of

all: open_shell

install: all
	cp crypto_mount /opt/usr/bin
	cp open_shell_in_cwd_of /opt/usr/bin
	cp png2pdf.sh /opt/usr/bin
	cp resize_win_at.sh /opt/usr/bin
	cp send_ip_on_change /opt/usr/bin
	cp ip_external.sh /opt/usr/bin

test:
	make -C test/ test

