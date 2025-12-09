
// stringify
#define a(x) X # x Y
#define b(x, y) @ # y # x @

a(11+12);
b(1*1, 2*2);

// connect
#define d(x) X ## x ## Y
d(8)
