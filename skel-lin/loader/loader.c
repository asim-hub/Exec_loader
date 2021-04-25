#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "exec_parser.h"
#define MAX 9999999
static so_exec_t *exec;

/* structura cu datele partajate de functii */
struct info_need
{
	struct sigaction sa_old;
	int file_desc;
	size_t dim_page;
} date;

/*vector cu paginile mapate */
unsigned int maped_page[MAX];
int nr_page;

static void handler(int sig_no, siginfo_t *si_info, void *cont)
{
	/* page offset si segment offset */
	int p_off, seg_off, id_p;
	int i, j, res;
	/* pointer la memoria mapata */
	void *r;

	/* caut segmentul care a provocat segfault */
	for (i = 0; i < exec->segments_no; i++)
		/* capete segment */
		if ((char *)si_info->si_addr >= (char *)exec->segments[i].vaddr &&
				(char *)si_info->si_addr < (char *)(exec->segments[i].vaddr + exec->segments[i].mem_size)) {
			/* index pagina */
			id_p = ((uintptr_t)si_info->si_addr - exec->segments[i].vaddr)/getpagesize();
			/* calculez offset segment */
			seg_off = (uintptr_t)si_info->si_addr - exec->segments[i].vaddr;

			/* cazul in care pagina in care s-a produs segfault a fost deja mapata */
			for (j = 0; j < nr_page; j++)
				if (maped_page[j] == id_p) {
					/* apelez handler default */
					date.sa_old.sa_sigaction(sig_no, si_info, cont);
					return;
				}

			seg_off -= seg_off % getpagesize();
			/* page offset*/
			p_off = exec->segments[i].offset + id_p * getpagesize();

			/* mapez o noua pagina (adresa care a produs sigsegv) in memorie */
			r = mmap((char *)exec->base_addr + id_p * getpagesize(), getpagesize(), PERM_R | PERM_W, MAP_FIXED | MAP_SHARED | MAP_ANONYMOUS, date.file_desc, 0);
			if (r == MAP_FAILED)
				exit(-1);

			/* repozitionez read file offset */
			lseek(date.file_desc, p_off, SEEK_SET);

			/* citesc datele*/
			res = read(date.file_desc, r, getpagesize());
			if (res < 0)
				exit(-1);

			/* setez protectia regiunii de memorie mapata */
			res = mprotect(r, getpagesize(), exec->segments[i].perm);
			if (res < 0)
				exit(-1);

			/* marchez pagina care a fost mapata */
			maped_page[nr_page++] = id_p;
			return;

		}

	/* daca adresa nu se gaseste in niciun segment, apelez handler default*/
	date.sa_old.sa_sigaction(sig_no, si_info, cont);

}

int so_init_loader(void)
{
	/* TODO: initialize on-demand loader */
	struct sigaction sa;
	int r;

	memset(&sa, 0, sizeof(sa));

	/* adaug handler definit mai sus */
	sa.sa_sigaction = handler;

	/* initializare semnal*/
	r = sigemptyset(&sa.sa_mask);
	if (r < 0)
		return SIGSEGV;

	/* adaug semnal */
	r = sigaddset(&sa.sa_mask, SIGSEGV);
	if (r < 0)
		return SIGSEGV;

	sa.sa_flags = SA_SIGINFO;

	r = sigaction(SIGSEGV, &sa, &date.sa_old);
	if (r < 0)
		return SIGSEGV;
	return 0;

}

int so_execute(char *path, char *argv[])
{
	exec = so_parse_exec(path);
	if (!exec)
		return -1;

	/* dimensiunea unei pagini */
	date.dim_page = getpagesize();

	/* file descripto fisier */
	date.file_desc = open(path, O_RDWR);

	if (date.file_desc < 0)
		exit(-1);
	so_start_exec(exec, argv);

	return -1;
}
