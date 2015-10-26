#include <stdio.h>

#define KERNEL_START  0xc0008000UL
#define KERNEL_SEARCH_START 0xc0000000UL
#define KERNEL_STOP   KERNEL_SEARCH_START + 1024 * 1024 * 400
#define MIN_LEN       10000UL

#define KSYM_NAME_LEN  128

unsigned long *ks_address = NULL;
unsigned long *ks_num = NULL;
unsigned char *ks_names = NULL;
unsigned long *ks_markers = NULL;
unsigned char *ks_token_tab = NULL;
unsigned short *ks_token_index = NULL;
unsigned long *ks_address_end = NULL;

unsigned long ksyms_pat[] = {0xc0008000, /* stext */
			     0xc0008000, /* _sinittext */
			     0xc0008000, /* _stext */
			     0xc0008000 /* __init_begin */
};
unsigned long ksyms_pat2[] = {0xc0008000, /* stext */
			      0xc0008000 /* _text */
};
unsigned long ksyms_pat3[] = {0xc00081c0, /* asm_do_IRQ */
			      0xc00081c0, /* _stext */
			      0xc00081c0 /* __exception_text_start */
};
/* MTK 3.4 内核 */
unsigned long ksyms_pat4[] = {0xc0008180, /* asm_do_IRQ */
			      0xc0008180, /* _stext */
			      0xc0008180 /* __exception_text_start */
};
/* 小米 2 */
unsigned long ksyms_pat5[] = {0xc0100000, /* asm_do_IRQ */
			      0xc0100000, /* _stext */
			      0xc0100000 /* __exception_text_start */
};
/* lovme */
unsigned long ksyms_pat6[] = {0x0,
			      0x1000,
			      0x1004,
			      0x1020,
			      0x10a0
};


static int checkPattern(unsigned long *addr, unsigned long *pattern, int patternnum) {
    unsigned long val, cnt, i;

    val = *addr;
    if (val == pattern[0]) {
        cnt = 1;
        for (i = 1; i < patternnum; i++) {
		val = *(addr + i);
            if (val == pattern[i]) {
                cnt++;
            } else {
                break;
            }
        }
	if (cnt == patternnum)
		return 0;
    }
    return 1;
}

static int check_pat(unsigned long *addr)
{
	unsigned long size;

	size = sizeof(unsigned long);
	if (checkPattern(addr, ksyms_pat, sizeof(ksyms_pat) / size) == 0) {
		return 0;
	} else if (checkPattern(addr, ksyms_pat2, sizeof(ksyms_pat2) / size) == 0) {
		return 0;
	} else if (checkPattern(addr, ksyms_pat3, sizeof(ksyms_pat3) / size) == 0) {
		return 0;
	} else if (checkPattern(addr, ksyms_pat4, sizeof(ksyms_pat4) / size) == 0) {
		return 0;
	} else if (checkPattern(addr, ksyms_pat5, sizeof(ksyms_pat5) / size) == 0) {
		return 0;
	} else if (checkPattern(addr, ksyms_pat6, sizeof(ksyms_pat6) / size) == 0) {
		return 0;
	}

	return 1;
}


int get_ksyms(void);

static unsigned long ks_expand_symbol(unsigned long off, char *namebuf)
{
        int len;
        int skipped_first;
        unsigned char *tptr;
        unsigned char *data;

        data = &ks_names[off];
        len = *data;
        off += len + 1;
        data++;

        skipped_first = 0;
        while (len > 0) {
                tptr = &ks_token_tab[ks_token_index[*data]];
                data++;
                len--;

                while(*tptr) {
                        if (skipped_first){
                                *namebuf = *tptr;
                                namebuf++;
                        }
                        else {
                                skipped_first = 1;
                        }
                        tptr++;
                }
        }
        *namebuf = '\0';
        return off;
}

static long _lookup_sym_part(const char *name)
{
        char namebuf[KSYM_NAME_LEN];
        unsigned long i;
        unsigned int off;

        if (ks_address == 0) {
                if(!get_ksyms())
                        return -1;
        }

        for (i = 0, off = 0; i < *ks_num; i++) {
                off = ks_expand_symbol(off, namebuf);
                if (strncmp(namebuf, name, strlen(name)) == 0)
                        return i;
        }
        return -1;
}

unsigned long lookup_sym_part(const char *name)
{
        long ret;

        ret = _lookup_sym_part(name);
        if (ret >= 0)
                return ks_address[ret];

        return 0;
}

unsigned long lookup_sym_part_next(const char *name)
{
        long ret;

        ret = _lookup_sym_part(name);
        if (ret >= 0)
                return ks_address[ret + 1];

        return 0;
}

unsigned long lookup_sym_part_pre(const char *name)
{
        long ret;

        ret = _lookup_sym_part(name);
        if (ret >= 0)
                return ks_address[ret - 1];

        return 0;
}

static long _lookup_sym(const char *name)
{
        char namebuf[KSYM_NAME_LEN];
        unsigned long i;
        unsigned int off;

        if (ks_address == 0) {
                if(!get_ksyms())
                        return -1;
        }

        for (i = 0, off = 0; i < *ks_num; i++) {
                off = ks_expand_symbol(off, namebuf);
                if (strcmp(namebuf, name) == 0)
                        return i;
        }
        return -1;
}

unsigned long lookup_sym(const char *name)
{
        long ret;
        ret = _lookup_sym(name);
        if (ret >= 0)
                return ks_address[ret];
        return 0;
}

unsigned long lookup_sym_next(const char *name)
{
        long ret;
        ret = _lookup_sym(name);
        if (ret >= 0)
                return ks_address[ret + 1];
        return 0;
}

unsigned long lookup_sym_pre(const char *name)
{
        long ret;
        ret = _lookup_sym(name);
        if (ret >= 0)
                return ks_address[ret - 1];
        return 0;
}

static unsigned short *find_kernel_symbol_token_index(void)
{
        int i = 0;

        while(ks_token_tab[i] || ks_token_tab[i + 1])
                i++;

        while (ks_token_tab[i] == 0)
                i++;
        
        return &ks_token_tab[i - 2];
}

static unsigned char *find_kernel_symbol_token_tab(void)
{
        unsigned long *addr;

        addr = &ks_markers[((*ks_num - 1) >> 8) + 1];

        while (*addr == 0)
                addr++;

        return addr;
}
static unsigned long *find_kernel_symbol_markers(void)
{
        unsigned long *addr;
        unsigned long i;
        unsigned long off;
        int len;

        for(i = 0, off = 0; i < *ks_num; i++) {
                len = ks_names[off];
                off += len + 1;
        }

        addr = (unsigned long*)((((unsigned long)&ks_names[off] - 1) | 0x3) + 1);

        while(*addr == 0)
                addr++;

        addr--;

        return addr;
}

static unsigned char *find_kernel_symbol_names(void)
{
        unsigned long *addr;

        addr = ks_num + 1;

        while(*addr == 0)
                addr++;

        return addr;
}

static unsigned long find_kernel_symbol_num(void)
{
        unsigned long *addr;
        unsigned long num = 0;
        int skip = 0;

        if (ks_address == 0)
                return NULL;

        addr = ks_address;
        
        while (*addr >= KERNEL_START) {
                addr++;
                num++;
        }

	ks_address_end = addr - 1;

        while (*addr == 0)
                addr++;

        return addr;
}

static void fix_symbol_tab_addr(void)
{
	if (ks_num == NULL)
		return;

	if ((ks_address_end - ks_address) != (*ks_num - 1))
		ks_address = ks_address_end - *ks_num + 1;
}


static unsigned long *find_kernel_symbol_tab(void)
{
        unsigned long *p;
        unsigned long *addr;
        unsigned num = 0;
        unsigned i = 0;

        p = KERNEL_START;


        while (p < KERNEL_STOP ) {
                addr = p;
                i = 0;
                if (*addr >= KERNEL_START) {
                        while ( i < MIN_LEN ) {
                                if (*(addr + 1) >= KERNEL_START
                                    && *(addr + 1) >= *addr) {
                                        addr++;
                                        i++;
                                        continue;
                                }
                                break;
                        }
                        
                        if (i == MIN_LEN) 
                                return p;
                }
                p += i+1;
        }

        return 0;
}

static unsigned long *find_kernel_symbol_tab_pat(void)
{
        unsigned long *p;

        p = KERNEL_START;

        while (p < KERNEL_STOP ) {
		if (check_pat(p) == 0)
			return p;
		p++;
	}
	return 0;
}

int get_ksyms(void)
{
        if (ks_address != 0)
                return 1;

	ks_address = find_kernel_symbol_tab_pat();
	if (ks_address == 0) {
		ks_address = find_kernel_symbol_tab();
	}
        if (ks_address == 0) {
                //printk("not find ksymbol_tab\n");
                return 0;
        }
        /* else { */
        /*         printk("kallsyms_address = %p\n", ks_address); */
        /* } */

        ks_num = find_kernel_symbol_num();
        /* if (ks_num) */
        /*         printk("kallsyms_num =%lu\n", *ks_num); */

	/* fix ks_address by ks_num */
	fix_symbol_tab_addr();

        ks_names = find_kernel_symbol_names();
        /* if (ks_names) */
        /*         printk("kallsyms_names addr = %p\n", ks_names); */

        ks_markers = find_kernel_symbol_markers();
        /* if (ks_markers) */
        /*         printk("kallsyms_markers addr = %p\n", ks_markers); */

        ks_token_tab = find_kernel_symbol_token_tab();
        /* if (ks_token_tab) */
        /*         printk("kallsyms_token_tab addr = %p\n", ks_token_tab); */
        ks_token_index = find_kernel_symbol_token_index();
        /* if (ks_token_index) */
        /*         printk("kallsyms_token_index addr = %p\n", ks_token_index); */
        
        return 1;
}
