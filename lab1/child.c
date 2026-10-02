#include <unistd.h>
#include <stdlib.h>
#include <string.h>

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

static int to_int(const char *s)
{
    int v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v;
}

static void write_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, buf + off, len - off);
        if (w <= 0) break;
        off += (size_t)w;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) return 1;
    int err_fd = to_int(argv[1]);   

    char *line;
    while ((line = read_line(STDIN_FILENO)) != NULL) {
        size_t len = strlen(line);

        size_t check_len = len;
        if (check_len > 0 && line[check_len - 1] == '\n')
            check_len--;

        int valid = 0;
        if (check_len > 0) {
            char last = line[check_len - 1];
            if (last == '.' || last == ';') valid = 1;
        }

        if (valid) {
            write_all(STDOUT_FILENO, line, len);
        } else {
            const char *msg = "INVALID: ";
            write_all(err_fd, msg, strlen(msg));
            write_all(err_fd, line, len);
            if (len == 0 || line[len - 1] != '\n')
                write_all(err_fd, "\n", 1);
        }
        free(line);
    }
    close(err_fd);
    return 0;
}