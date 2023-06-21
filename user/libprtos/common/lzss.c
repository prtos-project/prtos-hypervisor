/**************************************************************
        LZSS.C -- A Data compression Program
        (tab = 4 spaces)
        ***************************************************************
        4/6/1989 Haruhiko Okumura
        Use, distribute, and modify this program freely.
**************************************************************/

#include __PRTOS_INCFLD(compress.h)

#define N 4096 /* size of ring buffer */
#define F 18   /* upper limit for match_length */
#define THRESHOLD                                       \
    2         /* encode string into position and length \
                 if match_length is greater than this */
#define NIL N /* index for root of binary search trees */

static prtos_u8_t text_buf[N + F - 1]; /* ring buffer of size N, with extra
                                       F-1 bytes to facilitate string
                                       comparison */

static inline prtos_s32_t read_byte(prtos_u32_t *read_count, prtos_u32_t in_size, c_func_t read, void *read_data) {
    prtos_u8_t tmp;
    if (*read_count >= in_size) return -1;
    if (read(&tmp, 1, read_data) != 1) return -1;
    (*read_count)++;
    return tmp & 0xff;
}

static inline void write_byte(prtos_s32_t c, prtos_u32_t *write_count, prtos_u32_t out_size, c_func_t write, void *write_data) {
    prtos_u8_t tmp;
    if (*write_count >= out_size) return;
    tmp = c;
    if (write(&tmp, 1, write_data) != 1) return;
    (*write_count)++;
}

#ifndef _PRTOS_KERNEL_
static prtos_s32_t match_position, match_length, /* of longest match.  These are
                                                 set by the insert_node() procedure. */
    lson[N + 1], rson[N + 257], dad[N + 1];      /* left & right children &
                                                    parents -- These constitute binary search trees. */

static inline void init_tree(void) { /* initialize trees */
    prtos_s32_t i;

    /* For i = 0 to N - 1, rson[i] and lson[i] will be the right and
       left children of node i.  These nodes need not be initialized.
       Also, dad[i] is the parent of node i.  These are initialized to
       NIL (= N), which stands for 'not used.'
       For i = 0 to 255, rson[N + i + 1] is the root of the tree
       for strings that begin with character i.  These are initialized
       to NIL.  Note there are 256 trees. */

    for (i = N + 1; i <= N + 256; i++) rson[i] = NIL;
    for (i = 0; i < N; i++) dad[i] = NIL;
}

static inline void insert_node(prtos_s32_t r)
/* Inserts string of length F, text_buf[r..r+F-1], into one of the
   trees (text_buf[r]'th tree) and returns the longest-match position
   and length via the global variables match_position and match_length.
   If match_length = F, then removes the old node in favor of the new
   one, because the old one will be deleted sooner.
   Note r plays double role, as tree node and position in buffer. */
{
    prtos_s32_t i, p, cmp;
    prtos_u8_t *key;

    cmp = 1;
    key = &text_buf[r];
    p = N + 1 + key[0];
    rson[r] = lson[r] = NIL;
    match_length = 0;
    for (;;) {
        if (cmp >= 0) {
            if (rson[p] != NIL)
                p = rson[p];
            else {
                rson[p] = r;
                dad[r] = p;
                return;
            }
        } else {
            if (lson[p] != NIL)
                p = lson[p];
            else {
                lson[p] = r;
                dad[r] = p;
                return;
            }
        }
        for (i = 1; i < F; i++)
            if ((cmp = key[i] - text_buf[p + i]) != 0) break;
        if (i > match_length) {
            match_position = p;
            if ((match_length = i) >= F) break;
        }
    }
    dad[r] = dad[p];
    lson[r] = lson[p];
    rson[r] = rson[p];
    dad[lson[p]] = r;
    dad[rson[p]] = r;
    if (rson[dad[p]] == p)
        rson[dad[p]] = r;
    else
        lson[dad[p]] = r;
    dad[p] = NIL; /* remove p */
}

static inline void delete_node(prtos_s32_t p) /* deletes node p from tree */
{
    prtos_s32_t q;

    if (dad[p] == NIL) return; /* not in tree */
    if (rson[p] == NIL)
        q = lson[p];
    else if (lson[p] == NIL)
        q = rson[p];
    else {
        q = lson[p];
        if (rson[q] != NIL) {
            do {
                q = rson[q];
            } while (rson[q] != NIL);
            rson[dad[q]] = lson[q];
            dad[lson[q]] = dad[q];
            lson[q] = lson[p];
            dad[lson[p]] = q;
        }
        rson[q] = rson[p];
        dad[rson[p]] = q;
    }
    dad[q] = dad[p];
    if (rson[dad[p]] == p)
        rson[dad[p]] = q;
    else
        lson[dad[p]] = q;
    dad[p] = NIL;
}

prtos_s32_t lzss_compress(prtos_u32_t in_size, prtos_u32_t out_size, c_func_t read, void *read_data, c_func_t write, void *write_data) {
    prtos_s32_t i, c, len, r, s, last_match_length, code_buf_ptr;
    prtos_u8_t code_buf[17], mask;
    prtos_u32_t read_count = 0, write_count = 0;

    init_tree();     /* initialize trees */
    code_buf[0] = 0; /* code_buf[1..16] saves eight units of code, and
                        code_buf[0] works as eight flags, "1" representing that the unit
                        is an unencoded letter (1 byte), "0" a position-and-length pair
                        (2 bytes).  Thus, eight units require at most 16 bytes of code. */
    code_buf_ptr = mask = 1;
    s = 0;
    r = N - F;
    for (i = s; i < r; i++)
        text_buf[i] = ' '; /* Clear the buffer with
                              any character that will appear often. */
    for (len = 0; len < F && (c = read_byte(&read_count, in_size, read, read_data)) != -1; len++)
        text_buf[r + len] = c; /* read F bytes into the last F bytes of
                                  the buffer */
    if (len == 0) return 0;    /* text of size zero */
    for (i = 1; i <= F; i++)
        insert_node(r - i); /* Insert the F strings,
                              each of which begins with one or more 'space' characters.  Note
                              the order in which these strings are inserted.  This way,
                              degenerate trees will be less likely to occur. */
    insert_node(r);         /* Finally, insert the whole string just read.  The
                              global variables match_length and match_position are set. */
    do {
        if (match_length > len)
            match_length = len; /* match_length
                                    may be spuriously long near the end of text. */
        if (match_length <= THRESHOLD) {
            match_length = 1;                       /* Not long enough match.  Send one byte. */
            code_buf[0] |= mask;                    /* 'send one byte' flag */
            code_buf[code_buf_ptr++] = text_buf[r]; /* Send uncoded. */
        } else {
            code_buf[code_buf_ptr++] = (prtos_u8_t)match_position;
            code_buf[code_buf_ptr++] =
                (prtos_u8_t)(((match_position >> 4) & 0xf0) | (match_length - (THRESHOLD + 1))); /* Send position and
                                                                                                   length pair. Note match_length > THRESHOLD. */
        }
        if ((mask <<= 1) == 0) {                                                    /* Shift mask left one bit. */
            for (i = 0; i < code_buf_ptr; i++)                                      /* Send at most 8 units of */
                write_byte(code_buf[i], &write_count, out_size, write, write_data); /* code together */
            code_buf[0] = 0;
            code_buf_ptr = mask = 1;
        }
        last_match_length = match_length;
        for (i = 0; i < last_match_length && (c = read_byte(&read_count, in_size, read, read_data)) != -1; i++) {
            delete_node(s);  /* Delete old strings and */
            text_buf[s] = c; /* read new bytes */
            if (s < F - 1)
                text_buf[s + N] = c; /* If the position is
                                        near the end of buffer, extend the buffer to make
                                        string comparison easier. */
            s = (s + 1) & (N - 1);
            r = (r + 1) & (N - 1);
            /* Since this is a ring buffer, increment the position
               modulo N. */
            insert_node(r); /* Register the string in text_buf[r..r+F-1] */
        }
        while (i++ < last_match_length) { /* After the end of text, */
            delete_node(s);               /* no need to read, but */
            s = (s + 1) & (N - 1);
            r = (r + 1) & (N - 1);
            if (--len) insert_node(r); /* buffer may not be empty. */
        }
    } while (len > 0);      /* until length of string to be processed is zero */
    if (code_buf_ptr > 1) { /* Send remaining code. */
        for (i = 0; i < code_buf_ptr; i++) write_byte(code_buf[i], &write_count, out_size, write, write_data);
    }
    return write_count;
}
#endif

prtos_s32_t lzss_uncompress(prtos_u32_t in_size, prtos_u32_t out_size, c_func_t read, void *read_data, c_func_t write, void *write_data) {
    prtos_s32_t i, j, k, r, c;
    prtos_u32_t flags;
    prtos_u32_t read_count = 0, write_count = 0;

    for (i = 0; i < N - F; i++) text_buf[i] = ' ';
    r = N - F;
    flags = 0;
    for (;;) {
        if (((flags >>= 1) & 256) == 0) {
            if ((c = read_byte(&read_count, in_size, read, read_data)) == -1) break;
            flags = c | 0xff00; /* uses higher byte cleverly */
        }                       /* to count eight */
        if (flags & 1) {
            if ((c = read_byte(&read_count, in_size, read, read_data)) == -1) break;
            write_byte(c, &write_count, out_size, write, write_data);
            text_buf[r++] = c;
            r &= (N - 1);
        } else {
            if ((i = read_byte(&read_count, in_size, read, read_data)) == -1) break;
            if ((j = read_byte(&read_count, in_size, read, read_data)) == -1) break;
            i |= ((j & 0xf0) << 4);
            j = (j & 0x0f) + THRESHOLD;
            for (k = 0; k <= j; k++) {
                c = text_buf[(i + k) & (N - 1)];
                write_byte(c, &write_count, out_size, write, write_data);
                text_buf[r++] = c;
                r &= (N - 1);
            }
        }
    }

    return write_count;
}

#ifdef _PRTOS_KERNEL_
#define RWORD(w) w
#else
#include <endianess.h>
#endif

struct compress_hdr {
#define COMPRESS_MAGIC 0x64751423
    prtos_u32_t magic;
    prtos_u_size_t size;
    prtos_u_size_t lz_size;
};

#ifndef _PRTOS_KERNEL_
prtos_s32_t compress(prtos_u32_t in_size, prtos_u32_t out_size, c_func_t read, void *read_data, c_func_t write, void *write_data,
                     void (*SeekW)(prtos_s_size_t offset, void *write_data)) {
    struct compress_hdr hdr;
    prtos_u32_t lz_size;

    if (out_size <= (in_size + (in_size >> 1) + sizeof(struct compress_hdr))) return COMPRESS_BUFFER_OVERRUN;

    SeekW(sizeof(struct compress_hdr), write_data);
    if ((lz_size = lzss_compress(in_size, out_size - sizeof(struct compress_hdr), read, read_data, write, write_data)) <= 0) return COMPRESS_ERROR_LZ;
    hdr.magic = RWORD(COMPRESS_MAGIC);
    hdr.size = RWORD(in_size);
    hdr.lz_size = RWORD(lz_size);
    SeekW(-(lz_size + sizeof(struct compress_hdr)), write_data);
    if (write(&hdr, sizeof(struct compress_hdr), write_data) != sizeof(struct compress_hdr)) return COMPRESS_WRITE_ERROR;
    return (lz_size + sizeof(struct compress_hdr));
}
#endif

prtos_s32_t uncompress(prtos_u32_t in_size, prtos_u32_t out_size, c_func_t read, void *read_data, c_func_t write, void *write_data) {
    struct compress_hdr hdr;
    prtos_u32_t lz_size, size, uncom_size;

    if (read(&hdr, sizeof(struct compress_hdr), read_data) != sizeof(struct compress_hdr)) return COMPRESS_READ_ERROR;

    if (hdr.magic != RWORD(COMPRESS_MAGIC)) return COMPRESS_BAD_MAGIC;

    size = RWORD(hdr.size);
    lz_size = RWORD(hdr.lz_size);

    if (out_size < size) return COMPRESS_BUFFER_OVERRUN;

    uncom_size = lzss_uncompress(lz_size, out_size, read, read_data, write, write_data);
    return (size == uncom_size) ? uncom_size : -1;
}
