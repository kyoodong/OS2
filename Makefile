main: clear
	gcc ssu_shell.c -o ssu_shell
	./ssu_shell

debug: clear
	gcc ssu_shell.c -g -o ssu_shell_debug
	gdb ./ssu_shell_debug

clear:
	rm ssu_shell -f
