
#define A 2
#if A == 1
A
#elif A == 2
B
#else 
C
#endif

#define B 1
#ifdef a
A
#elif defined B
B
#endif

