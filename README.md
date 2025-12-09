# Tiny C preprocessor

Tiny C preprocessor just for my leaning of compiling process. Inspired by 
article of [https://www.sigbus.info/compilerbook](https://www.sigbus.info/compilerbook) and his chibicc project.

- default include paths are gcc ver13 headers.
- only -I option is covered.
- dev/tested on linux(Ubuntu 24.03.3) with gcc ver 13.3.
- no memory management like chibicc project.
- cover only limted predefined macros. not support \_\_DATE__, \_\_TIME__, etc.
