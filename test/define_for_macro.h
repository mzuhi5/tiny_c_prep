#define FOR_EACH(macro)  macro(Argument) macro(Basic)
#define VALUE(name)  name(ret v)

FOR_EACH(VALUE)
