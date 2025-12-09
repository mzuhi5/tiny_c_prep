
// simple
#define A(x) x
A(18)

// simple recursive
#define c(x) b(2+x)
#define b(x) c("@"x"@") AAA
#define a(x, y) b(x*y)
a(7, 8)
c(3)

// empty
#define h(x)
h(10)

// lacking arg
#define k(x, y, z) x+y+z
k(1, , 7)
