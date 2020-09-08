main: clear
	gcc ssu_shell.c -o ssu_shell
	./ssu_shell

clear:
	rm ssu_shell -f
