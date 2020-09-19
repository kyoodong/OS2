#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main() {
	char buffer[1024];
	int len;
	while ((len = read(0, buffer, 1024)) > 0) {
		write(1, buffer, len);
		printf("%d\n", len);
	}

	return 0;
}
