// @TODO: zombie 숫자 안맞음

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
#include <errno.h>
#include <pwd.h>
#include <sys/time.h>


// 프로세스 구조체
struct process {
	pid_t pid;          // pid
	char user[9];       // 유저 이름
	long priority;      // 우선 순위
	long nice;          // nice
	unsigned long virtual_memory;   // 가상 메모리 크기
	long resident_set_memory;       // rss 메모리 크기
	long shared_memory;             // 공유 메모리 크기
	char status;                    // 프로세스 상태
	float cpu_usage;                // cpu 사용량 (%)
	float memory_usage;             // 메모리 사용량 (%)
	unsigned long time;             // cpu 사용시간 (utime + stime)
	char command[64];               // 명령어
};

// 프로세스를 링크드 리스트로 관리하기 위한 노드
struct node {
	struct process process;
	struct node *prev, *next;
	int visit;
};

void data_refresh();


struct sigaction act;
struct sigaction act_alarm;
int user_process_count;
struct node *head, *tail;
struct node *top;
char buffer[512];

int sub_height, sub_width;
int width, height;
int x, y;
int ch;
time_t t;
struct tm tm;
struct timeval now, last_update_time;

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

int cpu_count;
int cpu_user;
int old_cpu_user;
int cpu_nice;
int old_cpu_nice;
int cpu_system;
int old_cpu_system;
int cpu_idle;
int old_cpu_idle;
int cpu_iowait;
int old_cpu_iowait;
int cpu_irq;
int old_cpu_irq;
int cpu_softirq;
int old_cpu_softirq;
int cpu_steal;
int old_cpu_steal;
int cpu_guest;
int cpu_guest_nice;
int all_cpu_total;
int old_all_cpu_total;
int cpu_total;
int old_cpu_total;

int mem_total;
int mem_free;
int mem_available;
int mem_buffer;
int mem_cache;
int mem_kreclaimable;
int swap_total;
int swap_free;


/**
 * 프로세스를 추가하는 함수
 * @param process 프로세스 객체
 */
void add_node(struct process process) {
	struct node *p = head;
	long hz = sysconf(_SC_CLK_TCK);

	// 이미 프로세스 리스트가 구축된 경우 이를 새로고침함
	while (p != NULL) {
	    // 같은 프로세스 발견
		if (p->process.pid == process.pid) {
		    // total cpu time의 변화
			int cpu_diff = cpu_total - old_cpu_total;

			// 그 동안 현재 프로세스가 사용한 cpu time
			int time_diff = process.time - p->process.time;

			if (cpu_diff > 0) {
				process.cpu_usage = 100. * time_diff / cpu_diff;
			}
			else
				process.cpu_usage = 0;

			// 해당 프로세스는 방문했음을 표시
			// 모든 프로세스를 검사한 뒤 visit == 0 인 프로세스는 죽은 프로세스로, 리스트에서 삭제
			p->visit = 1;
			p->process = process;
			return;
		}
		p = p->next;
	}

	// 프로세스 추가
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
	// pid 순으로 정렬
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

/**
 * 프로세스 상태 새로고침 전 visit 변수를 0으로 초기화해주는 함수
 */
void clear_visit() {
	struct node *p = head;
	while (p != NULL) {
		p->visit = 0;
		p = p->next;
	}
}

/**
 * 프로세스 노드를 삭제하는 함수
 * @param node 삭제할 노드
 */
void delete_node(struct node *node) {
	struct node *prev, *next;
	prev = node->prev;
	next = node->next;

	if (next != NULL) {
	    // 현재 화면에 표시중인 top 이 삭제된 경우
		if (node == top)
			top = next;

		if (node == head)
			head = next;
		next->prev = prev;
	}


	if (prev != NULL) {
        // 현재 화면에 표시중인 top 이 삭제된 경우
		if (node == top)
			top = prev;

		if (node == head)
			head = prev;
		prev->next = next;
	}

	free(node);
}

/**
 * 프로세스 새로고침 작업 뒤 죽은 프로세스를 없애주는 함수
 */
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

/**
 * 윈도우 삭제해주는 함수
 * exit 호출 시 실행되도록 등록
 */
void window_clear() {
	delwin(sub_window);
	delwin(main_window);
	endwin();
}

/**
 * 메인 프레임을 출력하는 함수
 * 상단부의 스크롤되지 않는 부분이 메인 프레임이다.
 */
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

	int diff = all_cpu_total - old_all_cpu_total;

	sprintf(buffer, "%s  %.1f us,  %.1f sy,  %.1f ni,  %.1f id,  %.1f wa,  %.1f hi,  %.1f si, %.1f st\n",
			"%%CPU(s):",
			(float) (cpu_user - old_cpu_user) / diff * 100,
			(float) (cpu_system - old_cpu_system) / diff * 100,
			(float) (cpu_nice - old_cpu_nice) / diff * 100,
			(float) (cpu_idle - old_cpu_idle) / diff * 100,
			(float) (cpu_iowait - old_cpu_iowait) / diff * 100,
			(float) (cpu_irq - old_cpu_irq) / diff * 100,
			(float) (cpu_softirq - old_cpu_softirq) / diff * 100,
			(float) (cpu_steal - old_cpu_steal) / diff * 100
	);
	buffer[width] = '\0';
	printw(buffer);

	sprintf(buffer, "KiB Mem : %8d total, %8d free, %8d used, %8d buff/cache\n",
			mem_total, mem_free, mem_total - (mem_free + mem_buffer + mem_cache + mem_kreclaimable), mem_buffer + mem_cache + mem_kreclaimable);
	buffer[width] = '\0';
	printw(buffer);

	sprintf(buffer, "KiB Swap: %8d total, %8d free, %8d used, %8d avail Mem\n",
			swap_total, swap_free, swap_total - swap_free, mem_available);
	buffer[width] = '\0';
	printw(buffer);

	buffer[0] = '\0';
	sprintf(buffer, "\n%6s%9s%8s%5s%11s%8s%8s %1s%7s%7s%10s COMMAND\n",
			"PID", "USER", "PR", "NI", "VIRT", "RES", "SHR", "S", "%%CPU", "%%MEM", "TIME+");
	buffer[width] = '\0';
	printw(buffer);
	wrefresh(main_window);
}

/**
 * 서브 윈도우를 출력해주는 함수
 * 메인 윈도우를 제외한 스크롤되는 프로세스 세부 상태를 표시하는 부분이 서브 윈도우이다.
 */
void print_sub() {
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
	werase(sub_window);

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
		wprintw(sub_window, buffer);

		node = node->next;
	}

	unsigned long t = node->process.time;
	unsigned long min, sec, mils;

	// hz -> 초 단위 변환
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
	buffer[sub_width] = '\0';
	wprintw(sub_window, buffer);

	wrefresh(sub_window);
}

void sig_handler(int signo) {
	exit(1);
}

/**
 * 알람 시그널을 처리하는 함수
 * @param signo
 */
void sig_alarm_handler(int signo) {
    // 데이터 새로고침
	data_refresh();

	// 출력
	print_main();
	print_sub();

	// 3초 뒤 알람 재지정
	alarm(3);
}


int main(int argc, char **argv) {
    // 윈도우 정리 함수 등록
	atexit(window_clear);
	act.sa_handler = sig_handler;
	act_alarm.sa_handler = sig_alarm_handler;

	sigemptyset(&act.sa_mask);
	sigemptyset(&act_alarm.sa_mask);

	sigaction(SIGINT, &act, NULL);
	sigaction(SIGABRT, &act, NULL);
	sigaction(SIGSEGV, &act, NULL);
	sigaction(SIGALRM, &act_alarm, NULL);

	// 3초 주기로 새로고침
	alarm(3);

	// 메인 윈도우 크기
	x = 0;
	y = 7;

	// 윈도우 생성
	if ((main_window = initscr()) == NULL) {
		fprintf(stderr, "initscr error\n");
		exit(1);
	}

	cbreak();
	noecho();

	// 전체 터미널 크기 측정
	getmaxyx(stdscr, height, width);

	// 데이터 새로고침
	data_refresh();

	// 메인 윈도우 출력
	print_main();

	// 서브 윈도우 생성
	sub_window = subwin(main_window, height - 7, width, y, x);

	// 키보드 입력 이벤트 등록
	keypad(stdscr, TRUE);

	// 스크롤 이벤트 등록
	scrollok(sub_window, TRUE);

	// 서브 윈도우 출력
	top = head;
	print_sub();


	struct node *node = NULL;

	// q를 누를때까지 반복
	while ((ch = getch()) != 'q') {
		switch (ch) {
		    // 화살표 윗키
		case KEY_UP:
			data_refresh();
			print_main();

			if (top->prev != NULL) {
				top = top->prev;
				wscrl(sub_window, -1);
			}
			print_sub();
			break;

			// 화살표 아래키
		case KEY_DOWN:
			data_refresh();
			print_main();

			node = top;
            data_refresh();
            print_main();

			// 스크롤 가능한지 검사
			for (int i = 0; i < sub_height; i++) {
				if (node == NULL)
					break;
				node = node->next;
			}

			if (node != NULL) {
				top = top->next;
				wscrl(sub_window, 1);
			}

			print_sub();
			break;
		}
	}
	refresh();
	exit(0);
}


/**
 * 프로세스 정보를 새로고침하는 함수
 */
void data_refresh() {
	// 현재 시간 구하기
	t = time(NULL);
	tm = *localtime(&t);
	gettimeofday(&now, NULL);

	// 0.5 초 주기로 새로고침. 너무 빠르게 새로고침하면 파일 오픈 시 에러가 발생함
	if (now.tv_usec < last_update_time.tv_usec) {
		now.tv_sec--;
		now.tv_usec += 1000000;
	}
	long diff = (now.tv_sec - last_update_time.tv_sec) * 1000000 + now.tv_usec - last_update_time.tv_usec;
	if (diff < 100000)
		return;

	last_update_time = now;

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


	// 메모리 사용량
	fp = fopen("/proc/meminfo", "r");
	fscanf(fp, "%*s %d kB", &mem_total);
	fscanf(fp, "%*s %d kB", &mem_free);
	fscanf(fp, "%*s %d kB", &mem_available);
	fscanf(fp, "%*s %d kB", &mem_buffer);
	fscanf(fp, "%*s %d kB", &mem_cache);
	fscanf(fp, "%*s %*d kB"); // swap cache
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
	fscanf(fp, "%*s %*d kB"); // Dirty
	fscanf(fp, "%*s %*d kB"); // Writeback
	fscanf(fp, "%*s %*d kB"); // AnonPage
	fscanf(fp, "%*s %*d kB"); // Mapped
	fscanf(fp, "%*s %*d kB"); // Shmem
	fscanf(fp, "%*s %d kB", &mem_kreclaimable); // KReclaimable
	fclose(fp);

	old_cpu_user = cpu_user;
	old_cpu_nice = cpu_nice;
	old_cpu_system = cpu_system;
	old_cpu_idle = cpu_idle;
	old_cpu_iowait = cpu_iowait;
	old_cpu_irq = cpu_irq;
	old_cpu_softirq = cpu_softirq;
	old_cpu_steal = cpu_steal;

	// CPU 사용량
	fp = fopen("/proc/stat", "r");
	fscanf(fp, "%*s %d %d %d %d %d %d %d %d %d %d",
			&cpu_user, &cpu_nice, &cpu_system, &cpu_idle,
			&cpu_iowait, &cpu_irq, &cpu_softirq, &cpu_steal, &cpu_guest, &cpu_guest_nice);

	// CPU 평균을 내기 위해 cpu 개수 세기
	char cpu_buf[10];
	cpu_count = 0;
	while (1) {
		fscanf(fp, "%s", cpu_buf);
		cpu_buf[3] = 0;
		if (!strcmp(cpu_buf, "cpu"))
			cpu_count++;
		else
			break;
		fscanf(fp, "%*[^\n]");
	}
	fclose(fp);

	// 총 cpu 시간을 측정한 뒤 평균
	old_cpu_total = cpu_total;
	cpu_total = cpu_user + cpu_system + cpu_nice + cpu_idle;

	old_all_cpu_total = all_cpu_total;
	all_cpu_total = cpu_total;
	cpu_total /= cpu_count;

	// 모든 프로세스 정리
	process_count = 0;
	running_process_count = 0;
	sleeping_process_count = 0;
	stopped_process_count = 0;
	zombie_process_count = 0;
	
	clear_non_visited_nodes();
	clear_visit();

	dp = opendir("/proc");
	if (dp != NULL) {
		while ((dir = readdir(dp)) != NULL) {
			if (!strcmp(dir->d_name, ".") || !strcmp(dir->d_name, ".."))
				continue;

			int process_id = atoi(dir->d_name);
			char status;
			char command[1024];
			long priority, nice, resident_set_memory, shared_memory;
			unsigned long utime, stime, cutime, cstime, virtual_memory;
			unsigned long long starttime;
			int ruid, euid, suid, fuid;

            // 디렉토리 이름이 숫자인 것들만 추출
            // 프로세스에 대한 디렉토리라면 디렉토리 이름은 pid 이기 때문
			if (process_id == 0)
				continue;

			// 명령어, 상태, cpu time, priority 등을 추출
			sprintf(buffer, "/proc/%d/stat", process_id);
			fp = fopen(buffer, "r");
			if (fp == NULL)
				continue;

			fscanf(fp, "%*d %s %c", command, &status);
			fscanf(fp, "%*d %*d %*d %*d %*d"); // ppid, pgrp, session, tty_nr, tpgid
			fscanf(fp, "%*d %*d %*d %*d %*d"); // flags, minflt, cminflt, majflt, cmajflt
			fscanf(fp, "%lu %lu %lu %lu", &utime, &stime, &cutime, &cstime);
			fscanf(fp, "%ld %ld", &priority, &nice);
			fscanf(fp, "%*d %*d"); // num_threads, itrealvalue
			fscanf(fp, "%lld %lu %ld", &starttime, &virtual_memory, &resident_set_memory);
			//fscanf(fp, "%*lu %*lu %*lu %*lu %*lu"); // rsslim, startcode, endcode, startstack, kstkesp
			//fscanf(fp, "%*lu %*lu %*lu %*lu %*lu"); // kstkeip, signal, blocked, sigignore, sigcatch 
			//fscanf(fp, "%*lu %*lu %*lu %*lu %*lu"); // wchan, nswap, cnswap, exit_signal, processor

			// command 괄호 제거
			command[strlen(command) - 1] = '\0';

			// Byte -> KB 단위
			virtual_memory /= 1024;
			fclose(fp);

			// 전체 메모리 크기 추출
			sprintf(buffer, "/proc/%d/statm", process_id);
			fp = fopen(buffer, "r");
			if (fp == NULL)
				continue;
			fscanf(fp, "%*d %ld %ld", &resident_set_memory, &shared_memory);
			fclose(fp);

			// 페이지 -> KB 단위 변환
			int page_size = getpagesize() / 1024;
			resident_set_memory *= page_size;
			shared_memory *= page_size;

			// uid 추출
			sprintf(buffer, "/proc/%d/status", process_id);
			fp = fopen(buffer, "r");
			if (fp == NULL)
				continue;

			while (fscanf(fp, "%s", buffer) > 0) {
				if (!strcmp(buffer, "State:")) {
					fscanf(fp, " %c %*[^\n]\n", &status);
				}
				else if (!strcmp(buffer, "Uid:")) {
					fscanf(fp, "%d%d%d%d\n", &ruid, &euid, &suid, &fuid);
				}
				else
					fscanf(fp, "%*[^\n]\n");
			}


			fclose(fp);

			switch (status) {
				case 'T':
					stopped_process_count++;
					break;

				case 'S':
				//case 'D':
					sleeping_process_count++;
					break;

				case 'R':
					running_process_count++;
					break;

				case 'Z':
					zombie_process_count++;
					break;

				default:
					break;
			}

			struct process process;

			process.pid = process_id;
			struct passwd *pw = getpwuid(ruid);
			if (pw != NULL) {
			    // 이름 길이 제한
				strncpy(process.user, pw->pw_name, 8);
				if (strlen(process.user) > sizeof(process.user)) {
					process.user[7] = '+';
					process.user[8] = '\0';
				}
			}

			process.priority = priority;
			process.nice = nice;
			process.virtual_memory = virtual_memory;
			process.resident_set_memory = resident_set_memory;
			process.shared_memory = shared_memory;
			process.status = status;
			process.time = utime + stime;
			process.cpu_usage = 0;
			process.memory_usage = 100. * resident_set_memory / mem_total;
			strcpy(process.command, command + 1);
			add_node(process);
			process_count++;
		}
		closedir(dp);
	}
}
