open_shell:
	g++ -Wall open_shell_in_cwd_of.cc -lX11 -lXmu -o open_shell_in_cwd_of

install:
	cp crypto_mount /opt/usr/bin
	cp png2pdf.sh /opt/usr/bin
	cp send_ip_on_change /opt/usr/bin
