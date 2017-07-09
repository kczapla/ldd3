#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main() {

    int quantum;
    int fd = open("/dev/scull0", O_APPEND);
    printf("%i\n", fd);
    quantum = ioctl(fd, 65031);
    close(fd);
    printf("%i\n", quantum);
    return 0;
}


