main: clear ttop pps
	gcc ssu_shell.c -o ssu_shell
	./ssu_shell

debug: clear
	gcc ssu_shell.c -g -o ssu_shell_debug -lncurses
#gdb ./ssu_shell_debug

ttop:
	gcc ttop.c -o ttop -lncurses

ttop_debug:
	gcc ttop.c -g -o ttop_debug -lncurses
	gdb ./ttop_debug

pps: clear
	gcc pps.c -o pps -lncurses

pps_debug: clear
	gcc pps.c -g -o pps_debug -lncurses
	gdb ./pps_debug

clear:
	rm ssu_shell ttop ttop_debug pps_debug -f
