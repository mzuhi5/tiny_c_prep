#define B 1
#if B == 1
A
#endif

#if B == 1 &&  0
B
#endif

#if B == 1 ||  0
C
#endif

#if B >  0
D
#endif

#if B  <  0
E
#endif

#if B <=  0
F
#endif

#if B >=  0
G
#endif

#if B != 0
H
#endif
