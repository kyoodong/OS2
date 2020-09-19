#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <pwd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#define COMMAND_SIZE 512

// 프로세스 구조체
struct process {
	char user[9];
	int pid;
	float cpu_usage;
	float memory_usage;
	int virtual_memory_size;
	int resident_set_memory_size;
	char tty[8];
	char status[20];
	struct tm starttime;
	unsigned long cumulative_cpu_time;
	char command[COMMAND_SIZE];
};

// 프로세스 링크드 리스트 노드
struct node {
	struct node *prev;
	struct node *next;
	struct process process;
};

char buffer[1024];
struct node *head;
struct node *tail;
int uptime;
int a_opt, u_opt, x_opt;

int hz;
int width, height;

// 옵션마다 출력되는 항목이 다른데 flag 가 on 되면 해당 내용이 화면에 출력됨
int user_flag;
int pid_flag = 1;
int cpu_flag;
int mem_flag;
int vsz_flag;
int rss_flag;
int tty_flag = 1;
int stat_flag;
int start_flag;
int time_flag = 1;
int command_flag = 1;
int time_length = 8;

struct passwd *passwd;


/**
 * 프로세스 노드를 추가하는 함수
 * @param process
 */
void add_node(struct process process) {
	if (head == NULL) {
		head = malloc(sizeof(struct node));
		head->prev = head->next = NULL;
		head->process = process;
		tail = head;
		return;
	}

	// 맨 끝에 하나씩 추가함
	struct node *node = malloc(sizeof(struct node));
	node->prev = tail;
	node->next = NULL;
	tail->next = node;
	node->process = process;
	tail = node;
}

int main(int argc, char **argv) {
	pid_t pid, root_session_id;
	FILE *fp;
	DIR *dp;
	struct dirent *dir;
	char command[COMMAND_SIZE];
	uid_t euid, root_euid, uid;
	unsigned long utime, stime;
	int cpu_user, cpu_nice, cpu_system, cpu_idle;
	int cpu_total, mem_total;
	struct winsize w;

	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
	width = w.ws_col;

	// 아규먼트 파싱
	if (argc > 1) {
		for (int i = 0; i < 3; i++) {
		    // a 옵션
			if (argv[1][i] == 'a') {
				a_opt = 1;
				stat_flag = 1;
				time_length = 4;
			}

			// u 옵션
			if (argv[1][i] == 'u') {
				u_opt = 1;
				user_flag = 1;
				cpu_flag = 1;
				mem_flag = 1;
				vsz_flag = 1;
				rss_flag = 1;
				stat_flag = 1;
				start_flag = 1;
				time_length = 4;
			}

			// x 옵션
			if (argv[1][i] == 'x') {
				x_opt = 1;
				stat_flag = 1;
				time_length = 4;
			}
		}
	}

	pid = getpid();

	// 세션 아이디 추출
	sprintf(buffer, "/proc/%d/stat", pid);
	fp = fopen(buffer, "r");
	if (fp == NULL) {
		fprintf(stderr, "%s open error", buffer);
		exit(1);
	}
	fscanf(fp, "%*d %*s %*c %*d %*d %d", &root_session_id);
	fclose(fp);

	// euid 추출
	root_euid = geteuid();

	// uptime 추출
	fp = fopen("/proc/uptime", "r");
	if (fp == NULL) {
		fprintf(stderr, "/proc/uptime open error\n");
		exit(1);
	}
	fscanf(fp, "%d", &uptime);
	fclose(fp);

	// 전체 메모리 크기 추출
	fp = fopen("/proc/meminfo", "r");
	fscanf(fp, "%*s %d kB", &mem_total);
	fclose(fp);

	hz = sysconf(_SC_CLK_TCK);

	dp = opendir("/proc");
	if (dp == NULL) {
		fprintf(stderr, "/proc open error");
		exit(1);
	}

	while ((dir = readdir(dp)) != NULL) {
		if (!strcmp(dir->d_name, ".") || !strcmp(dir->d_name, ".."))
			continue;

		int process_id = atoi(dir->d_name);
		int session_id, tpgid, pgrp, vmlck;
		struct process process;
		long resident_set_memory, process_uptime, nice, priority;
		unsigned long virtual_memory;
		unsigned long long starttime;
		char tty[256];
		char status;
		int processor;

        // 디렉토리가 숫자로 이루어진 디렉토리만 취급함
        // 숫자로 이루어진 디렉토리만이 해당 pid 의 프로세스에 대한 정보를 갖고 있기 때문
		if (!process_id)
			continue;

		sprintf(buffer, "/proc/%d/stat", process_id);
		fp = fopen(buffer, "r");
		if (fp == NULL) {
			fprintf(stderr, "%s open error", buffer);
			exit(1);
		}

		// 명령어, 상태, 그룹 아이디, 세션 아이디, foreground 그룹 아이디, cpu 사용 시간, 우선순위, 프로세서 등 추출
		fscanf(fp, "%*d %s %c %*d %d %d", command, &status, &pgrp, &session_id);
		fscanf(fp, "%*d %d %*d %*d %*d %*d %*d", &tpgid);
		fscanf(fp, "%lu %lu %*d %*d", &utime, &stime);
		fscanf(fp, "%ld %ld %*d %*d %llu %lu %ld", &priority, &nice, &starttime, &virtual_memory, &resident_set_memory);
		fscanf(fp, "%*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %d", &processor);
		fclose(fp);

		// Byte -> KB 단위
		virtual_memory /= 1024;

		// page -> KB 단위
		int page_size = getpagesize() / 1024;
		resident_set_memory *= page_size;

		starttime /= hz;
		process_uptime = uptime - starttime;
		time_t t = time(NULL);
		t -= process_uptime;
		struct tm tm = *localtime(&t);

		// uid, euid, vmlck 여부 추출
		strcat(buffer, "us");
		fp = fopen(buffer, "r");
		fscanf(fp, "%*s %[^\n]\n", command);
		fscanf(fp, "%*[^\n]\n");
		fscanf(fp, "%*[^\n]\n");
		fscanf(fp, "%*[^\n]\n");
		fscanf(fp, "%*[^\n]\n");
		fscanf(fp, "%*[^\n]\n");
		fscanf(fp, "%*[^\n]\n");
		fscanf(fp, "%*[^\n]\n");
		fscanf(fp, "%s %d %d", buffer, &uid, &euid);
		fscanf(fp, "%*[^\n]\n");
		fscanf(fp, "%*[^\n]\n");
		fscanf(fp, "%*[^\n]\n");
		fscanf(fp, "%*[^\n]\n");
		fscanf(fp, "%*[^\n]\n");
		fscanf(fp, "%*[^\n]\n");
		fscanf(fp, "%*[^\n]\n");
		fscanf(fp, "%*[^\n]\n");
		fscanf(fp, "%*[^\n]\n");
		fscanf(fp, "%*[^\n]\n");
		fscanf(fp, "%*s %d", &vmlck);
		fclose(fp);

		memset(process.status, 0, sizeof(process.status));
		process.status[0] = status;

		fp = fopen("/proc/stat", "r");
		fscanf(fp, "%*[^\n]\n");
	
		while (1) {
			char cpu_buf[20];
			char *cp;
			int cpu_num;

			fscanf(fp, "%s", cpu_buf);
			cp = cpu_buf + 3;
			cpu_num = atoi(cp);

			// 프로세스가 돌고 있는 프로세서의 총 동작 시간 추출
			if (cpu_num == processor) {
				fscanf(fp, "%d %d %d %d", &cpu_user, &cpu_nice, &cpu_system, &cpu_idle);
				break;
			}
			fscanf(fp, "%*[^\n]");
		}
		fclose(fp);

		cpu_total = (cpu_user + cpu_system + cpu_nice + cpu_idle);

        memset(tty, 0, sizeof(tty));
        sprintf(buffer, "/proc/%d/fd/0", session_id);
		int pass_flag = 1;
	
		// 아무 옵션 없는 경우
		if (!a_opt && !u_opt && !x_opt) {
			if (session_id != root_session_id || euid != root_euid)
				pass_flag = 1;

			else if (readlink(buffer, tty, sizeof(tty)) < 0) {
				pass_flag = 1;
			}
			else {
				pass_flag = 0;
			}
		}

		// a, u 옵션 둘 중 하나라도 있는 경우
		if (pass_flag && (a_opt || u_opt)) {
			if (readlink(buffer, tty, sizeof(tty)) < 0) {
				pass_flag = 1;
			}
			else if (!strcmp(tty, "/dev/null"))
				pass_flag = 1;
			// u옵션만 있는 경우 euid 가 같아야함
			else if (!a_opt && u_opt && euid != root_euid)
				pass_flag = 1;
			else
				pass_flag = 0;
		}
		
		if (pass_flag && x_opt) {
			if (readlink(buffer, tty, sizeof(tty)) < 0) {
				pass_flag = 1;
			}
			// euid 가 같아야함
			if (euid != root_euid)
				pass_flag = 1;
			else
				pass_flag = 0;
		}

		// a, x 옵션 같이 있으면 모든 프로세스 출력
		if (!(a_opt && x_opt) && pass_flag)
			continue;

		// 옵션이 하나라도 있는 경우
		if (a_opt || x_opt || u_opt) {
		    // 약식 명령어가 아닌 풀 명령어를 구해옴
			char buf[COMMAND_SIZE];
			sprintf(buffer, "/proc/%d/cmdline", process_id);
			fp = fopen(buffer, "r");
			memset(buf, 0, sizeof(buf));
			while (1) {
				memset(buffer, 0, sizeof(buffer));
				int status = fscanf(fp, "%s", buffer);
				if (status != 1)
					break;

				// 명령어와 아규먼트가 null문자로 구분되어 있어서 다음과 같이 추출함
				for (int i = 0; i < sizeof(buffer); i++) {
					if (buffer[i] == '\0') {
						if (buffer[i + 1] == '\0')
							break;
						else
							buffer[i] = ' ';
					}
				}

				strcat(buf, buffer);
				strcat(buf, " ");
			}
			fclose(fp);

			// 풀 명령어가 존재하면 command 에 적용
			if (strlen(buf) > 0) {
				strcpy(command, buf);
			}

			// 존재하지 않으면 기존 명령어에 []를 붙여줌
			else {
				int length = strlen(command);
				for (int i = length - 1; i >= 0; i--) {
					command[i + 1] = command[i];
				}
				command[0] = '[';
				command[length + 1] = ']';
				command[length + 2] = '\0';
			}

			// 백그라운드 스레드의 수를 셈
			sprintf(buffer, "/proc/%d/task", process_id);
			DIR *dp = opendir(buffer);
			struct dirent *dir;
			int count = 0;
			while ((dir = readdir(dp)) != NULL) {
				if (!strcmp(dir->d_name, ".") || !strcmp(dir->d_name, ".."))
					continue;

				count++;
			}

			// STAT 부가 정보
			if (nice < 0)
				strcat(process.status, "<");

			if (nice > 0)
				strcat(process.status, "N");

			if (vmlck != 0)
				strcat(process.status, "L");

			if (process_id == session_id)
				strcat(process.status, "s");

			if (count > 1)
				strcat(process.status, "l");

			if (pgrp == tpgid)
				strcat(process.status, "+");
		}

		process.pid = process_id;
		passwd = getpwuid(uid);
		memset(process.user, 0, sizeof(process.user));
		strncpy(process.user, passwd->pw_name, sizeof(process.user));
		if (strlen(passwd->pw_name) > sizeof(process.user)) {
			process.user[sizeof(process.user) - 1] = '+';
			process.user[sizeof(process.user)] = '\0';
		}
		if (!strcmp(tty, "/dev/null") || strlen(tty) == 0)
			strcpy(process.tty, "?");
		else
			strcpy(process.tty, tty + 5);
		strcpy(process.command, command);
		process.cumulative_cpu_time = utime + stime;
		process.virtual_memory_size = virtual_memory;
		process.resident_set_memory_size = resident_set_memory;
		process.cpu_usage = 100 * (float) process.cumulative_cpu_time / cpu_total;
		process.memory_usage = 100. * resident_set_memory / mem_total;
		process.starttime = tm;
		add_node(process);
	}
	closedir(dp);

	// 출력
	struct node *node = head;

	if (user_flag)
		printf("%-9s ", "USER");

	if (pid_flag)
		printf("%4s ", "PID");

	if (cpu_flag)
		printf("%4s ", "%CPU");

	if (mem_flag)
		printf("%4s ", "%MEM");

	if (vsz_flag)
		printf("%8s ", "VSZ");

	if (rss_flag)
		printf("%8s ", "RSS");

	if (tty_flag)
		printf("%-9s ", "TTY");

	if (stat_flag)
		printf("%-5s ", "STAT");

	if (start_flag)
		printf("%-5s ", "START");

	if (time_flag) {
		if (time_length == 8)
			printf("%8s ", "TIME");
		else if (time_length == 4)
			printf("%4s ", "TIME");
	}

	if (command_flag)
		printf("COMMAND");

	printf("\n");

	while (node) {
		unsigned long t = node->process.cumulative_cpu_time / hz;
		int hour, min, sec;
		hour = t / (60 * 60);
		t %= (60 * 60);
		min = t / 60;
		t %= 60;
		sec = t;

		char *cp = buffer;
		memset(buffer, 0, sizeof(buffer));
		if (user_flag)
			cp += sprintf(cp, "%-9s ", node->process.user);

		if (pid_flag)
			cp += sprintf(cp, "%4d ", node->process.pid);

		if (cpu_flag)
			cp += sprintf(cp, "%4.1f ", node->process.cpu_usage);

		if (mem_flag)
			cp += sprintf(cp, "%4.1f ", node->process.memory_usage);
	
		if (vsz_flag)
			cp += sprintf(cp, "%8d ", node->process.virtual_memory_size);
	
		if (rss_flag)
			cp += sprintf(cp, "%8d ", node->process.resident_set_memory_size);
	
		if (tty_flag)
			cp += sprintf(cp, "%-9s ", node->process.tty);
	
		if (stat_flag)
			cp += sprintf(cp, "%-5s ", node->process.status);
	
		if (start_flag)
			cp += sprintf(cp, "%02d:%02d ", node->process.starttime.tm_hour, node->process.starttime.tm_min);
	
		if (time_flag) {
			if (time_length == 8)
				cp += sprintf(cp, "%02d:%02d:%02d ", hour, min, sec);
			else if (time_length == 4)
				cp += sprintf(cp, "%d:%02d ", min, sec);
		}
	
		if (command_flag)
			cp += sprintf(cp, "%s", node->process.command);
	
		buffer[width] = '\0';
		printf("%s\n", buffer);
		node = node->next;
	}
	exit(0);
}

