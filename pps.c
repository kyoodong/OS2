#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <pwd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#define COMMAND_SIZE 512

struct process {
	char user[8];
	int pid;
	float cpu_usage;
	float memory_usage;
	int virtual_memory_size;
	int resident_set_memory_size;
	char tty[8];
	char status[20];
	unsigned long start;
	unsigned long cumulative_cpu_time;
	char command[COMMAND_SIZE];
};

struct node {
	struct node *prev;
	struct node *next;
	struct process process;
};

struct winsize w;
char buffer[1024];
struct node *head;
struct node *tail;
int uptime;
int a_opt, u_opt, x_opt;

int width, height;
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


void add_node(struct process process) {
	if (head == NULL) {
		head = malloc(sizeof(struct node));
		head->prev = head->next = NULL;
		head->process = process;
		tail = head;
		return;
	}

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

	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
	width = w.ws_col;

	if (argc > 1) {
		for (int i = 0; i < 3; i++) {
			if (argv[1][i] == 'a') {
				a_opt = 1;
				stat_flag = 1;
				time_length = 4;
			}
			if (argv[1][i] == 'u')
				u_opt = 1;
			if (argv[1][i] == 'x') {
				x_opt = 1;
				stat_flag = 1;
				time_length = 4;
			}
		}
	}

	pid = getpid();
	sprintf(buffer, "/proc/%d/stat", pid);
	fp = fopen(buffer, "r");
	if (fp == NULL) {
		fprintf(stderr, "%s open error", buffer);
		exit(1);
	}
	fscanf(fp, "%*d %*s %*c %*d %*d %d", &root_session_id);
	fclose(fp);

	root_euid = geteuid();

	fp = fopen("/proc/uptime", "r");
	if (fp == NULL) {
		fprintf(stderr, "/proc/uptime open error\n");
		exit(1);
	}
	fscanf(fp, "%d", &uptime);
	fclose(fp);

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
		int hz;
		long resident_set_memory;
		unsigned long virtual_memory, priority, nice;
		unsigned long long starttime, process_uptime;
		char tty[256];
		char status;


		if (!process_id)
			continue;

		sprintf(buffer, "/proc/%d/stat", process_id);
		fp = fopen(buffer, "r");
		if (fp == NULL) {
			fprintf(stderr, "%s open error", buffer);
			exit(1);
		}
		fscanf(fp, "%*d %s %c %*d %d %d", command, &status, &pgrp, &session_id);
		fscanf(fp, "%*d %d %*d %*d %*d %*d %*d", &tpgid);
		fscanf(fp, "%lu %lu %*d %*d", &utime, &stime);
		fscanf(fp, "%ld %ld %*d %*d %llu %lu %ld", &priority, &nice, &starttime, &virtual_memory, &resident_set_memory);
		fclose(fp);

		// Byte -> KB 단위
		virtual_memory /= 1024;

		int page_size = getpagesize() / 1024;
		resident_set_memory *= page_size;

		hz = sysconf(_SC_CLK_TCK);
		process_uptime = uptime - (starttime / hz);

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

		memset(tty, 0, sizeof(tty));
		sprintf(buffer, "/proc/%d/fd/0", session_id);
		
		memset(process.status, 0, sizeof(process.status));
		process.status[0] = status;

		int pass_flag = 0;

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

		if (pass_flag && a_opt) {
			if (readlink(buffer, tty, sizeof(tty)) < 0) {
				pass_flag = 1;
			}
			else if (!strcmp(tty, "/dev/null"))
				pass_flag = 1;
			else
				pass_flag = 0;
		}
		
		if (pass_flag && x_opt) {
			if (readlink(buffer, tty, sizeof(tty)) < 0) {
				pass_flag = 1;
			}
			if (euid != root_euid)
				pass_flag = 1;
			else
				pass_flag = 0;
		}

		if (pass_flag)
			continue;

		if (a_opt || x_opt) {
			char buf[COMMAND_SIZE];
			sprintf(buffer, "/proc/%d/cmdline", process_id);
			fp = fopen(buffer, "r");
			memset(buf, 0, sizeof(buf));
			while (1) {
				memset(buffer, 0, sizeof(buffer));
				int status = fscanf(fp, "%s", buffer);
				if (status != 1)
					break;

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

			if (strlen(buf) > 0) {
				strcpy(command, buf);
			} else {
				int length = strlen(command);
				for (int i = length - 1; i >= 0; i--) {
					command[i + 1] = command[i];
				}
				command[0] = '[';
				command[length + 1] = ']';
				command[length + 2] = '\0';
			}

			sprintf(buffer, "/proc/%d/task", process_id);
			DIR *dp = opendir(buffer);
			struct dirent *dir;
			int count = 0;
			while ((dir = readdir(dp)) != NULL) {
				if (!strcmp(dir->d_name, ".") || !strcmp(dir->d_name, ".."))
					continue;

				count++;
			}

			if (nice > 0)
				strcat(process.status, "<");

			if (nice < 0)
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
		strncpy(process.user, passwd->pw_name, sizeof(process.user) - 1);
		if (strlen(passwd->pw_name) > sizeof(process.user)) {
			process.user[sizeof(process.user) - 2] = '+';
		}
		if (!strcmp(tty, "/dev/null") || strlen(tty) == 0)
			strcpy(process.tty, "?");
		else
			strcpy(process.tty, tty + 5);
		strcpy(process.command, command);
		process.cumulative_cpu_time = (utime + stime) / hz;
		process.virtual_memory_size = virtual_memory;
		process.resident_set_memory_size = resident_set_memory;
		process.cpu_usage = 0;
		process.memory_usage = 0;
		add_node(process);
	}
	closedir(dp);

	struct node *node = head;

	if (user_flag)
		printf("%8s ", "USER");

	if (pid_flag)
		printf("%6s ", "PID");

	if (cpu_flag)
		printf("%5s ", "%%CPU");

	if (mem_flag)
		printf("%5s ", "%%MEM");

	if (vsz_flag)
		printf("%6s ", "VSZ");

	if (rss_flag)
		printf("%6s ", "RSS");

	if (tty_flag)
		printf("%-12s ", "TTY");

	if (stat_flag)
		printf("%-6s ", "STAT");

	if (start_flag)
		printf("%8s ", "START");

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
		unsigned long t = node->process.cumulative_cpu_time;
		int hour, min, sec;
		hour = t / (60 * 60);
		t %= (60 * 60);
		min = t / 60;
		t %= 60;
		sec = t;

		char *cp = buffer;
		memset(buffer, 0, sizeof(buffer));
		if (user_flag)
			cp += sprintf(cp, "%8s ", node->process.user);

		if (pid_flag)
			cp += sprintf(cp, "%6d ", node->process.pid);

		if (cpu_flag)
			cp += sprintf(cp, "%5f ", node->process.cpu_usage);

		if (mem_flag)
			cp += sprintf(cp, "%5f ", node->process.memory_usage);
	
		if (vsz_flag)
			cp += sprintf(cp, "%6d ", node->process.virtual_memory_size);
	
		if (rss_flag)
			cp += sprintf(cp, "%6d ", node->process.resident_set_memory_size);
	
		if (tty_flag)
			cp += sprintf(cp, "%-12s ", node->process.tty);
	
		if (stat_flag)
			cp += sprintf(cp, "%-6s ", node->process.status);
	
		if (start_flag)
			cp += sprintf(cp, "%8lu ", node->process.start);
	
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

