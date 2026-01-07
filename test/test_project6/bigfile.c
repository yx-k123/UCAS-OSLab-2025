#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char buff[64];

int main(void)
{
    sys_touch("2.txt");
    int fd = sys_open("2.txt", O_RDWR);

    // 1. Write to the beginning
    char *start_str = "Start of Big File\n";
    sys_write(fd, start_str, strlen(start_str));

    // 2. Seek to 128MB (128 * 1024 * 1024)
    int offset = 128 * 1024 * 1024;
    sys_lseek(fd, offset, SEEK_SET);

    // 3. Write to the end
    char *end_str = "End of Big File\n";
    sys_write(fd, end_str, strlen(end_str));

    // 4. Verify Start
    sys_lseek(fd, 0, SEEK_SET);
    memset(buff, 0, sizeof(buff));
    sys_read(fd, buff, strlen(start_str));
    printf("Read from start: %s", buff);

    // 5. Verify End
    sys_lseek(fd, offset, SEEK_SET);
    memset(buff, 0, sizeof(buff));
    sys_read(fd, buff, strlen(end_str));
    printf("Read from end: %s", buff);

    sys_close(fd);

    return 0;
}