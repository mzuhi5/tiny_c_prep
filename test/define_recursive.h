
// simple recursive
#define a(x) a(x-1)
a(10) // a(10-1)

// recursive in argument
#define a0(x) c0(b0(x))
#define b0(x) c0(a0(x))
#define c0(x) @x@
a0(1) // @@a0(1)@@

// mix of argument and func expansion
#define a1(f) f(4)
#define b1(x) x
a1(b1(b1)) // b1(4)

#define a2(x) !x!
a2(a2(3)) // !!3!!
