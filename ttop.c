#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <utmp.h>
#include <dirent.h>
#include <string.h>


char buffer[1024];


int main(int argc, char **argv) {
	// 현재 시간 구하기
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);

	FILE *fp;
	int uptime;

	// 부팅 시간 구하기
	fp = fopen("/proc/uptime", "r");
	if (fp == NULL) {
		fprintf(stderr, "/proc/uptime open error\n");
		exit(1);
	}

	fscanf(fp, "%d", &uptime);
	fclose(fp);

	// 부팅 시간 계산
	int uptime_hour = uptime / (60 * 60);
	int uptime_min = uptime % (60 * 60) / 60;
	struct utmp utmp;

	int user_count = 0;

	// 접속 사용자 로그
	fp = fopen("/var/run/utmp", "r");
	while (fread(&utmp, sizeof(struct utmp), 1, fp) == 1) {
		// 유저 프로세스만 세기
		if (utmp.ut_type == USER_PROCESS)
			user_count++;
	}
	fclose(fp);

	// load average
	fp = fopen("/proc/loadavg", "r");
	float cpu_average_mean_for_1min;
	float cpu_average_mean_for_5min;
	float cpu_average_mean_for_15min;

	fscanf(fp, "%f %f %f", &cpu_average_mean_for_1min, &cpu_average_mean_for_5min, &cpu_average_mean_for_15min);
	fclose(fp);

	int process_count = 0;
	int running_process_count = 0;
	int sleeping_process_count = 0;
	int stopped_process_count = 0;
	int zombie_process_count = 0;
	struct dirent *dir;
	DIR *dp;
	dp = opendir("/proc");

	if (dp != NULL) {
		while ((dir = readdir(dp)) != NULL) {
			if (!strcmp(dir->d_name, ".") || !strcmp(dir->d_name, ".."))
				continue;

			int process_id = atoi(dir->d_name);
			char status;
			if (process_id == 0)
				continue;

			sprintf(buffer, "/proc/%d/stat", process_id);
			fp = fopen(buffer, "r");
			fscanf(fp, "%*d %*s %c", &status);

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
					printf("%c\n", status);
					break;
			}
			process_count++;
		}
	}

	printf("top - %d:%d:%d up %d:%d, %d user, load average: %.2f, %.2f, %.2f\n",
			tm.tm_hour,	// 시
			tm.tm_min,	// 분
			tm.tm_sec,	// 초
			uptime_hour,	// 서버 부팅 시간(시)
			uptime_min,		// 서버 부팅 시간(분)
			user_count,
			cpu_average_mean_for_1min,
			cpu_average_mean_for_5min,
			cpu_average_mean_for_15min);
	printf("Tasks:  %d total,  %d running,  %d sleeping,  %d stopped,  %d zombie\n",
			process_count,
			running_process_count,
			sleeping_process_count,
			stopped_process_count,
			zombie_process_count);
	exit(0);
}

