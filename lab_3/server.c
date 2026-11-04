#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <errno.h>
#define MAX_LINE 8192

struct slot {
    size_t len;
    int eof;
    char data[MAX_LINE];
};

static char *read_line(int fd)
{
    size_t cap = 128, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    char c; ssize_t n;
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

static void itoa_buf(int v, char *out)
{
    char tmp[16]; int t = 0, i = 0;
    if (v == 0) tmp[t++] = '0';
    while (v > 0) { tmp[t++] = '0' + v % 10; v /= 10; }
    while (t > 0) out[i++] = tmp[--t];
    out[i] = '\0';
}
static void must_sem_wait(sem_t *s)
{
    while (sem_wait(s) < 0) {
        if (errno == EINTR) continue;
        write(STDERR_FILENO, "sem_wait failed\n", 16);
        _exit(1);
    }
}

static void must_sem_post(sem_t *s)
{
    if (sem_post(s) < 0) {
        write(STDERR_FILENO, "sem_post failed\n", 16);
        _exit(1);
    }
}
int main(void)
{
    char name[512];
    write(STDOUT_FILENO, "Enter file name: ", 17);
    size_t i = 0; char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != '\n' && i < sizeof(name) - 1)
        name[i++] = c;
    name[i] = '\0';

    int fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { write(STDERR_FILENO, "open failed\n", 12); return 1; }
    if (dup2(fd, STDOUT_FILENO) < 0) { write(STDERR_FILENO, "dup2 failed\n", 12); return 1; }
    close(fd);

    int pid = (int)getpid();
    char num[16]; itoa_buf(pid, num);

    char shm1_name[64], shm2_name[64];
    char s1r_name[64], s1f_name[64], s2r_name[64], s2f_name[64];

    strcpy(shm1_name, "/lab3_shm1_"); strcat(shm1_name, num);
    strcpy(shm2_name, "/lab3_shm2_"); strcat(shm2_name, num);
    strcpy(s1r_name,  "/lab3_s1r_");  strcat(s1r_name,  num);
    strcpy(s1f_name,  "/lab3_s1f_");  strcat(s1f_name,  num);
    strcpy(s2r_name,  "/lab3_s2r_");  strcat(s2r_name,  num);
    strcpy(s2f_name,  "/lab3_s2f_");  strcat(s2f_name,  num);

    int shm1_fd = shm_open(shm1_name, O_CREAT | O_RDWR, 0600);
    int shm2_fd = shm_open(shm2_name, O_CREAT | O_RDWR, 0600);
    if (shm1_fd < 0 || shm2_fd < 0) {
        write(STDERR_FILENO, "shm_open failed\n", 16);
        return 1;
    }
    if (ftruncate(shm1_fd, sizeof(struct slot)) < 0 ||
        ftruncate(shm2_fd, sizeof(struct slot)) < 0) {
        write(STDERR_FILENO, "ftruncate failed\n", 17);
        return 1;
    }

    struct slot *to_client = mmap(NULL, sizeof(struct slot), PROT_READ | PROT_WRITE,
                                  MAP_SHARED, shm1_fd, 0);
    struct slot *to_server = mmap(NULL, sizeof(struct slot), PROT_READ | PROT_WRITE,
                                  MAP_SHARED, shm2_fd, 0);
    if (to_client == MAP_FAILED || to_server == MAP_FAILED) {
        write(STDERR_FILENO, "mmap failed\n", 12);
        return 1;
    }
    close(shm1_fd);
    close(shm2_fd);

    to_client->len = 0; to_client->eof = 0;
    to_server->len = 0; to_server->eof = 0;

    sem_t *s1_ready = sem_open(s1r_name, O_CREAT, 0600, 0);
    sem_t *s1_free  = sem_open(s1f_name, O_CREAT, 0600, 1);
    sem_t *s2_ready = sem_open(s2r_name, O_CREAT, 0600, 0);
    sem_t *s2_free  = sem_open(s2f_name, O_CREAT, 0600, 1);
    if (s1_ready == SEM_FAILED || s1_free == SEM_FAILED ||
        s2_ready == SEM_FAILED || s2_free == SEM_FAILED) {
        write(STDERR_FILENO, "sem_open failed\n", 16);
        return 1;
    }
    pid_t pidc = fork();
    if (pidc < 0) { write(STDERR_FILENO, "fork failed\n", 12); return 1; }

    if (pidc == 0) {
        execl("./client", "client",
              shm1_name, shm2_name, s1r_name, s1f_name, s2r_name, s2f_name, NULL);
        _exit(1);
    }

    char *line;
    while ((line = read_line(STDIN_FILENO)) != NULL) {
        size_t len = strlen(line);
        if (len > MAX_LINE) len = MAX_LINE;

        must_sem_wait(s1_free);       
        memcpy(to_client->data, line, len);
        to_client->len = len;
        to_client->eof = 0;
        must_sem_post(s1_ready);        

        free(line);
    }
    must_sem_wait(s1_free);
    to_client->len = 0;
    to_client->eof = 1;
    must_sem_post(s1_ready);
    for (;;) {
        must_sem_wait(s2_ready);         
        size_t len = to_server->len;
        int eof = to_server->eof;
        if (len > 0 && write(STDOUT_FILENO, to_server->data, len) != (ssize_t)len) {
            write(STDERR_FILENO, "write failed\n", 13);
            _exit(1);
        }
        must_sem_post(s2_free);         
        if (eof) break;
    }

    if (wait(NULL) < 0) {
        write(STDERR_FILENO, "wait failed\n", 12);
    }
    munmap(to_client, sizeof(struct slot));
    munmap(to_server, sizeof(struct slot));
    sem_close(s1_ready); sem_close(s1_free);
    sem_close(s2_ready); sem_close(s2_free);
    sem_unlink(s1r_name); sem_unlink(s1f_name);
    sem_unlink(s2r_name); sem_unlink(s2f_name);
    shm_unlink(shm1_name); shm_unlink(shm2_name);

    return 0;
}