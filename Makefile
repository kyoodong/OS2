main: clear
	gcc ssu_shell.c -o ssu_shell
	gcc ttop.c -o ttop -lncurses
	./ttop
#./ssu_shell

debug: clear
	gcc ssu_shell.c -g -o ssu_shell_debug -lncurses
	gcc ttop.c -g -o ttop_debug -lncurses
	gdb ./ttop_debug
#gdb ./ssu_shell_debug

clear:
	rm ssu_shell ttop -f
