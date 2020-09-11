#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <utmp.h>
#include <dirent.h>
#include <string.h>
#include <curses.h>
#include <signal.h>
#include <fcntl.h>
#include <pwd.h>

struct process {
	pid_t pid;
	char user[9];
	long priority;
	long nice;
	unsigned long virtual_memory;
	long resident_set_memory;
	long shared_memory;
	char status;
	float cpu_usage;
	float memory_usage;
	unsigned long time;
	char command[64];
};

struct node {
	struct process process;
	struct node *prev, *next;
	int visit;
};

struct user {
	int uid;
	char name[9];
};

void data_refresh();


struct sigaction act;
struct sigaction act_alarm;
int user_process_count;
struct node *head, *tail;
struct node *top;
char buffer[512];

int width, height;
int x, y;
int ch;
time_t t, last_update_time;
struct tm tm;

FILE *fp;

int uptime;
int uptime_hour;
int uptime_min;
struct utmp utmp;
int user_count = 0;

float cpu_average_mean_for_1min;
float cpu_average_mean_for_5min;
float cpu_average_mean_for_15min;

int process_count;
int running_process_count;
int sleeping_process_count;
int stopped_process_count;
int zombie_process_count;
struct dirent *dir;
DIR *dp;

int cpu_user;
int cpu_nice;
int cpu_system;
int cpu_idle;
int cpu_iowait;
int cpu_irq;
int cpu_softirq;
int cpu_steal;
int cpu_total;
int old_cpu_total;
int cpu_total_diff;
int sum;


int mem_total;
int mem_free;
int mem_available;
int mem_buffer;
int mem_cache;
int swap_total;
int swap_free;

void add_node(struct process process) {
	struct node *p = head;
	long hz = sysconf(_SC_CLK_TCK);

	while (p != NULL) {
		if (p->process.pid == process.pid) {
			int cpu_diff = process.time - p->process.time;
			sum += cpu_diff;
			
			p->process = process;
			if (cpu_total_diff > 0) {
				p->process.cpu_usage = 100 * cpu_diff / cpu_total_diff;
			}
			p->visit = 1;
			return;
		}
		p = p->next;
	}

	if (head == NULL) {
		head = malloc(sizeof(struct node));
		head->process = process;
		head->next = NULL;
		head->prev = NULL;
		tail = head;
		head->visit = 1;
		return;
	}

	p = head;
	while (p != NULL && p->process.pid < process.pid) {
		p = p->next;
	}

	if (p == NULL)
		p = tail;

	struct node *new_node = malloc(sizeof(struct node));
	if (p->next != NULL) {
		p->next->prev = new_node;
		new_node->next = p->next;
	} else {
		new_node->next = NULL;
	}

	p->next = new_node;
	new_node->prev = p;
	new_node->process = process;
	new_node->visit = 1;

	if (new_node->next == NULL)
		tail = new_node;
}

void clear_visit() {
	struct node *p = head;
	while (p != NULL) {
		p->visit = 0;
		p = p->next;
	}
}

void delete_node(struct node *node) {
	struct node *prev, *next;
	prev = node->prev;
	next = node->next;

	if (next != NULL) {
		if (node == top)
			top = next;

		if (node == head)
			head = next;
		next->prev = prev;
	}


	if (prev != NULL) {
		if (node == top)
			top = prev;

		if (node == head)
			head = prev;
		prev->next = next;
	}

	free(node);
}

void clear_non_visited_nodes() {
	struct node *p = head;
	struct node *next;
	while (p != NULL) {
		if (p->visit == 0) {
			next = p->next;
			delete_node(p);
			p = next;
			continue;
		}
		p = p->next;
	}
}

WINDOW *main_window, *sub_window;

void window_clear() {
	delwin(sub_window);
	delwin(main_window);
	endwin();
}

void print_main() {
	getmaxyx(main_window, height, width);
	char buffer[1024];

	sprintf(buffer, "top - %02d:%02d:%02d up %02d:%02d, %d user, load average: %.2f, %.2f, %.2f\n",
			tm.tm_hour,	// 시
			tm.tm_min,	// 분
			tm.tm_sec,	// 초
			uptime_hour,	// 서버 부팅 시간(시)
			uptime_min,		// 서버 부팅 시간(분)
			user_process_count,
			cpu_average_mean_for_1min,
			cpu_average_mean_for_5min,
			cpu_average_mean_for_15min);

	buffer[width] = '\0';
	mvwprintw(main_window, 0, 0, buffer); 

	sprintf(buffer, "Tasks:  %d total,  %d running,  %d sleeping,  %d stopped,  %d zombie\n",
			process_count,
			running_process_count,
			sleeping_process_count,
			stopped_process_count,
			zombie_process_count);
	buffer[width] = '\0';
	printw(buffer);

	sprintf(buffer, "%s  %.1f us,  %.1f sy,  %.1f ni,  %.1f id,  %.1f wa,  %.1f hi,  %.1f si, %.1f st\n",
			"%%CPU",
			(float) cpu_user / cpu_total * 100,
			(float) cpu_system / cpu_total * 100,
			(float) cpu_nice / cpu_total * 100,
			(float) cpu_idle / cpu_total * 100,
			(float) cpu_iowait / cpu_total * 100,
			(float) cpu_irq / cpu_total * 100,
			(float) cpu_softirq / cpu_total * 100,
			(float) cpu_steal / cpu_total * 100
	);
	buffer[width] = '\0';
	printw(buffer);

	sprintf(buffer, "KiB Mem : %d total,  %d free,  %d used,  %d buff/cache\n",
			mem_total, mem_free, mem_total - mem_free, mem_buffer + mem_cache);
	buffer[width] = '\0';
	printw(buffer);

	sprintf(buffer, "KiB Swap:  %d total,  %d free, %d used,  %d avail Mem\n",
			swap_total, swap_free, swap_total - swap_free, mem_available);
	buffer[width] = '\0';
	printw(buffer);

	buffer[0] = '\0';
	sprintf(buffer, "\n%6s%9s%8s%5s%11s%8s%8s %1s%7s%7s%10s COMMAND\n",
			"PID", "USER", "PR", "NI", "VIRT", "RES", "SHR", "S", "%%CPU", "%%MEM", "TIME+");
	buffer[width] = '\0';
	printw(buffer);
}

void print_sub() {
	int sub_height, sub_width;
	getmaxyx(sub_window, sub_height, sub_width);
	
	long hz = sysconf(_SC_CLK_TCK);
	struct node *node = top;

	for (int i = 0; i < sub_height - 1; i++) {
		if (node == NULL)
			return;
		node = node->next;
	}

	if (node == NULL) {
		top = top->prev;
		return;
	}

	char buffer[1024];
	char time_buffer[12];
	node = top;
	wmove(sub_window, 0, 0);

	// 출력
	for (int i = 0; i < sub_height-1; i++) {
		if (node->next == NULL)
			break;

		unsigned long t = node->process.time;
		unsigned long min, sec, mils;

		min = t / (hz * 60);
		t %= (60 * hz);
		sec = t / hz;
		mils = t % hz;

		sprintf(time_buffer, "%lu:%02lu.%02lu", min, sec, mils);
		sprintf(buffer, "%6d%9s%8ld%5ld%11lu%8ld%8ld %c%6.1f%6.1f%10s %s\n",
				node->process.pid,
				node->process.user,
				node->process.priority,
				node->process.nice,
				node->process.virtual_memory,
				node->process.resident_set_memory,
				node->process.shared_memory,
				node->process.status,
				node->process.cpu_usage,
				node->process.memory_usage,
				time_buffer,
				node->process.command
		);
		buffer[sub_width] = '\0';
		buffer[sub_width] = '\n';
		wprintw(sub_window, buffer);

		node = node->next;
	}

	unsigned long t = node->process.time;
	unsigned long min, sec, mils;

	min = t / (hz * 60);
	t %= (60 * hz);
	sec = t / hz;
	mils = t % hz;

	sprintf(time_buffer, "%lu:%02lu.%02lu", min, sec, mils);
	sprintf(buffer, "%6d%9s%8ld%5ld%11lu%8ld%8ld %c%6.1f%6.1f%10s %s",
			node->process.pid,
			node->process.user,
			node->process.priority,
			node->process.nice,
			node->process.virtual_memory,
			node->process.resident_set_memory,
			node->process.shared_memory,
			node->process.status,
			node->process.cpu_usage,
			node->process.memory_usage,
			time_buffer,
			node->process.command
	);
	buffer[sub_width - 1] = '\0';
	buffer[sub_width] = '\n';
	wprintw(sub_window, buffer);

	wrefresh(sub_window);

}

void sig_handler(int signo) {
	exit(1);
}

void sig_alarm_handler(int signo) {
	data_refresh();
	print_main();
	print_sub();

	alarm(3);
}


int main(int argc, char **argv) {
	int fd = open("err.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
	dup2(fd, 2);

	atexit(window_clear);
	act.sa_handler = sig_handler;
	act_alarm.sa_handler = sig_alarm_handler;

	sigemptyset(&act.sa_mask);
	sigemptyset(&act_alarm.sa_mask);

	sigaction(SIGINT, &act, NULL);
	sigaction(SIGABRT, &act, NULL);
	sigaction(SIGSEGV, &act, NULL);
	sigaction(SIGALRM, &act_alarm, NULL);
	alarm(3);
	x = 0;
	y = 7;

	if ((main_window = initscr()) == NULL) {
		fprintf(stderr, "initscr error\n");
		exit(1);
	}

	cbreak();
	noecho();

	getmaxyx(stdscr, height, width);

	fp = fopen("/etc/passwd", "r");
	if (fp == NULL) {
		fprintf(stderr, "/etc/passwd open error\n");
		exit(1);
	}

	char buf[64];
	int uid;
	while (fscanf(fp, "%[^:]:%*[^:]:%d", buf, &uid) == 2) {
		userlist[user_count].uid = uid;
		strncpy(userlist[user_count].name, buf, 8);
		if (strlen(buf) > 8)
			userlist[user_count].name[7] = '+';
		userlist[user_count].name[8] = '\0';

		user_count++;
		fscanf(fp, "%*[^\n]");
		fgetc(fp);
	}
	fclose(fp);

	data_refresh();

	print_main();

	sub_window = subwin(main_window, height - 7, width, y, x);
	keypad(stdscr, TRUE);

	scrollok(sub_window, TRUE);

	top = head;
	print_sub();

	while ((ch = getch()) != 'q') {
		switch (ch) {
		case KEY_UP:
			data_refresh();
			print_main();

			if (top->prev == NULL)
				break;

			top = top->prev;
			wscrl(sub_window, -1);
			print_sub();
			break;

		case KEY_DOWN:
			data_refresh();
			print_main();

			top = top->next;
			wscrl(sub_window, 1);
			print_sub();
			break;
		}
	}
	refresh();
	exit(0);
}


void data_refresh() {
	// 현재 시간 구하기
	t = time(NULL);
	tm = *localtime(&t);

	clear_non_visited_nodes();
	clear_visit();

	last_update_time = t;

	// 부팅 시간 구하기
	fp = fopen("/proc/uptime", "r");
	if (fp == NULL) {
		fprintf(stderr, "/proc/uptime open error\n");
		exit(1);
	}

	fscanf(fp, "%d", &uptime);
	fclose(fp);

	// 부팅 시간 계산
	uptime_hour = uptime / (60 * 60);
	uptime_min = uptime % (60 * 60) / 60;
	user_process_count = 0;

	// 접속 사용자 로그
	fp = fopen("/var/run/utmp", "r");
	if (fp == NULL) {
		fprintf(stderr, "/var/run/utmp open error\n");
		exit(1);
	}
	while (fread(&utmp, sizeof(struct utmp), 1, fp) == 1) {
		// 유저 프로세스만 세기
		if (utmp.ut_type == USER_PROCESS)
			user_process_count++;
	}
	fclose(fp);

	// load average
	fp = fopen("/proc/loadavg", "r");
	if (fp == NULL) {
		fprintf(stderr, "/proc/loadavg open error\n");
		exit(1);
	}
	fscanf(fp, "%f %f %f", &cpu_average_mean_for_1min, &cpu_average_mean_for_5min, &cpu_average_mean_for_15min);
	fclose(fp);

	old_cpu_total = cpu_total;

	// CPU 사용량
	fp = fopen("/proc/stat", "r");
	fscanf(fp, "%*s %d %d %d %d %d %d %d %d",
			&cpu_user, &cpu_nice, &cpu_system, &cpu_idle,
			&cpu_iowait, &cpu_irq, &cpu_softirq, &cpu_steal);
	fclose(fp);

	cpu_total = cpu_user + cpu_nice + cpu_system + cpu_idle + cpu_iowait + cpu_irq + cpu_softirq + cpu_steal;
	//cpu_total = cpu_user + cpu_system;

	fprintf(stderr, "cpu_total_diff = %d\n", cpu_total_diff);
	cpu_total_diff = cpu_total - old_cpu_total;
	sum = 0;

	// 메모리 사용량
	fp = fopen("/proc/meminfo", "r");
	fscanf(fp, "%*s %d kB", &mem_total);
	fscanf(fp, "%*s %d kB", &mem_free);
	fscanf(fp, "%*s %d kB", &mem_available);
	fscanf(fp, "%*s %d kB", &mem_buffer);
	fscanf(fp, "%*s %d kB", &mem_cache);
	fscanf(fp, "%*s %*d kB");
	fscanf(fp, "%*s %*d kB");
	fscanf(fp, "%*s %*d kB");
	fscanf(fp, "%*s %*d kB");
	fscanf(fp, "%*s %*d kB");
	fscanf(fp, "%*s %*d kB");
	fscanf(fp, "%*s %*d kB");
	fscanf(fp, "%*s %*d kB");
	fscanf(fp, "%*s %*d kB");
	fscanf(fp, "%*s %d kB", &swap_total);
	fscanf(fp, "%*s %d kB", &swap_free);
	fclose(fp);

	// 모든 프로세스 정리
	process_count = 0;
	running_process_count = 0;
	sleeping_process_count = 0;
	stopped_process_count = 0;
	zombie_process_count = 0;
	dp = opendir("/proc");

	if (dp != NULL) {
		while ((dir = readdir(dp)) != NULL) {
			if (!strcmp(dir->d_name, ".") || !strcmp(dir->d_name, ".."))
				continue;

			int process_id = atoi(dir->d_name);
			char status;
			char command[1024];
			int uid;
			long priority, nice, resident_set_memory, shared_memory;
			unsigned long utime, stime, virtual_memory;
			unsigned long long starttime;
			int ruid, euid, suid, fuid;

			if (process_id == 0)
				continue;

			sprintf(buffer, "/proc/%d/stat", process_id);
			fp = fopen(buffer, "r");
			if (fp == NULL)
				continue;

			fscanf(fp, "%*d %s %c", command, &status);
			fscanf(fp, "%*d %*d %*d %*d %*d"); // ppid, pgrp, session, tty_nr, tpgid
			fscanf(fp, "%*d %*d %*d %*d %*d"); // flags, minflt, cminflt, majflt, cmajflt
			fscanf(fp, "%lu %lu %*d %*d", &utime, &stime);
			fscanf(fp, "%ld %ld", &priority, &nice);
			fscanf(fp, "%*d %*d"); // num_threads, itrealvalue
			fscanf(fp, "%lld %lu %ld", &starttime, &virtual_memory, &resident_set_memory);
			//fscanf(fp, "%*lu %*lu %*lu %*lu %*lu"); // rsslim, startcode, endcode, startstack, kstkesp
			//fscanf(fp, "%*lu %*lu %*lu %*lu %*lu"); // kstkeip, signal, blocked, sigignore, sigcatch 
			//fscanf(fp, "%*lu %*lu %*lu %*lu %*lu"); // wchan, nswap, cnswap, exit_signal, processor

			fclose(fp);

			sprintf(buffer, "/proc/%d/statm", process_id);
			fp = fopen(buffer, "r");
			if (fp == NULL)
				continue;

			fscanf(fp, "%*d %ld %ld", &resident_set_memory, &shared_memory);
			fclose(fp);

			sprintf(buffer, "/proc/%d/status", process_id);
			fp = fopen(buffer, "r");
			if (fp == NULL)
				continue;

			fscanf(fp, "%*s %s", command);
			fgetc(fp);
			fscanf(fp, "%*[^\n]"); // umask
			fgetc(fp);
			fscanf(fp, "%*[^\n]"); // State
			fgetc(fp);
			fscanf(fp, "%*[^\n]"); // Tgid
			fgetc(fp);
			fscanf(fp, "%*[^\n]"); // Ngid
			fgetc(fp);
			fscanf(fp, "%*[^\n]"); // Pid
			fgetc(fp);
			fscanf(fp, "%*[^\n]"); // PPid
			fgetc(fp);
			fscanf(fp, "%*[^\n]"); // TracerPid
			fgetc(fp);
			fscanf(fp, "%*s%d%d%d%d", &ruid, &euid, &suid, &fuid);
			fclose(fp);

			int i;
			for (i = 0; i < user_count; i++) {
				if (userlist[i].uid == ruid) {
					break;
				}
			}

			switch (status) {
				case 'T':
					stopped_process_count++;
					break;

				case 'S':
					sleeping_process_count++;
					break;

				case 'R':
					running_process_count++;
					break;

				case 'Z':
					zombie_process_count++;
					break;

				default:
					//printf("%c\n", status);
					break;
			}

			struct process process;
			struct passwd *passwd = getpwuid(uid);

			process.pid = process_id;
			strncpy(process.user, passwd->pw_name, 8);
			if (strlen(passwd->pw_name) > 8)
				process.user[7] = '\n';
			process.user[8] = '\0';
			process.priority = priority;
			process.nice = nice;
			process.virtual_memory = virtual_memory;
			process.resident_set_memory = resident_set_memory;
			process.shared_memory = shared_memory;
			process.status = status;
			process.time = utime + stime;
			process.cpu_usage = 0;
			process.memory_usage = 100 * resident_set_memory / mem_total;
			strcpy(process.command, command);
			add_node(process);
			process_count++;
		}
		closedir(dp);
	}
}
