#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>

#define BUF_SIZE 4096
static char *read_line(int fd)
{
    size_t cap = 128, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;

    char c;
    ssize_t n;
    while ((n = read(fd, &c, 1)) == 1) {
        if (len + 2 > cap) {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) { free(buf); return NULL; }
            buf = tmp;
        }
        buf[len++] = c;
        if (c == '\n') break;
    }
    if (n <= 0 && len == 0) { free(buf); return NULL; }

    buf[len] = '\0';
    return buf;
}

int main(void)
{
    char name[512];
    write(STDOUT_FILENO, "Enter file name: ", 17);

    size_t i = 0;
    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != '\n' && i < sizeof(name) - 1)
        name[i++] = c;
    name[i] = '\0';

    int fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        write(STDERR_FILENO, "Cannot open file\n", 17);
        return 1;
    }
    if (dup2(fd, STDOUT_FILENO) < 0) {
        write(STDERR_FILENO, "dup2 failed\n", 12);
        return 1;
    }
    close(fd);
    int p1[2], p2[2];
    if (pipe(p1) < 0 || pipe(p2) < 0) {
        write(STDERR_FILENO, "pipe failed\n", 12);
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        write(STDERR_FILENO, "fork failed\n", 12);
        return 1;
    }

    if (pid == 0) {
        close(p1[1]);         
        close(p2[0]);             

        if (dup2(p1[0], STDIN_FILENO) < 0) _exit(1);
        close(p1[0]);

        char p2fd_str[16];
        int len = 0;
        int fdnum = p2[1];
        char tmp[16]; int t = 0;
        if (fdnum == 0) tmp[t++] = '0';
        while (fdnum > 0) { tmp[t++] = '0' + fdnum % 10; fdnum /= 10; }
        while (t > 0) p2fd_str[len++] = tmp[--t];
        p2fd_str[len] = '\0';

        execl("./child", "child", p2fd_str, NULL);
        _exit(1);
    }

    close(p1[0]); 
    close(p2[1]);   

    char *line;
    while ((line = read_line(STDIN_FILENO)) != NULL) {
        size_t len = strlen(line);
        size_t written = 0;
        while (written < len) {
            ssize_t w = write(p1[1], line + written, len - written);
            if (w <= 0) break;
            written += (size_t)w;
        }
        free(line);
    }
    close(p1[1]); 

    char buf[BUF_SIZE];
    ssize_t n;
    while ((n = read(p2[0], buf, sizeof(buf))) > 0) {
        write(STDOUT_FILENO, buf, (size_t)n);
    }
    close(p2[0]);

    wait(NULL);
    return 0;
}