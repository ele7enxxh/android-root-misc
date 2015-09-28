#include <stdio.h>

#define KERNEL_START  0xc0008000UL
#define KERNEL_SEARCH_START 0xc0000000UL
#define KERNEL_STOP   KERNEL_SEARCH_START + 1024 * 1024 * 400
#define MIN_LEN       10000UL

#define KSYM_NAME_LEN  128

unsigned long *ks_address = 0;
unsigned long *ks_num = 0;
unsigned char *ks_names = 0;
unsigned long *ks_markers = 0;
unsigned char *ks_token_tab = 0;
unsigned short *ks_token_index = 0;

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

        while (*addr == 0)
                addr++;

        return addr;
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


int get_ksyms(void)
{
        if (ks_address != 0)
                return 1;
        
        ks_address = find_kernel_symbol_tab();
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
