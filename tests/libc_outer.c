#include <stdio.h>
#include <math.h>
int inner(char*,int);
int main(){ char buf[512]; inner(buf,512); puts(buf); printf("expect atan1=%.6f tan=%.6f sin=%.6f\n", atan(1.0), tan(0.5), sin(2.0)); }
