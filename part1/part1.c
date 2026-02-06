#include <unistd.h>

int main()
{
	sleep(1);
	getpid();
	getuid();
	getppid();
	write(1, "X\n", 2);
	return 0;
}
