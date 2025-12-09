// undef
#define B 1
#ifdef B
A
#endif

#undef B

#ifdef B
C
#endif

// variable length argument
#define a1(...) @(__VA_ARGS__)@
a1(1, 2, 3)
