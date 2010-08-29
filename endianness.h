#ifndef ENDIANNESS_H
#define ENDIANNESS_H

#define L_ENDIAN 0
#define B_ENDIAN    1

int get_endianness();

unsigned long long swpd(double d);
double uswpd(unsigned long long d);

float swapf(float);
double swapd(double);
void swaps(unsigned short *x);
void swapi(unsigned int *x);
void swapl(unsigned long *x);

#endif
