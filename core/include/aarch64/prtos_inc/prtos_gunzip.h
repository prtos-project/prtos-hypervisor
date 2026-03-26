#ifndef __PRTOS_GUNZIP_H
#define __PRTOS_GUNZIP_H

int gzip_check(char *image, unsigned long image_len);
int perform_gunzip(char *output, char *image, unsigned long image_len);

#endif
