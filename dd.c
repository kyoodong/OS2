#include <stdio.h>
#include <pwd.h>

int main() {
	struct passwd *pw = getpwuid(62583);
	if (pw == NULL) {
		fprintf(stderr, "err");
		exit(1);
	}

	printf("%s\n", pw->pw_name);
	return 0;
}
