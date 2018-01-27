liblatalloc - Latency-logging allocator
=======================================

This allocator implements the C malloc API, but instead of doing any
allocation itself it records timing information about another malloc
implementation using `dlsym`.

It is built as a linux shared library, usable as a replacement allocator by:

```sh
$ LD_PRELOAD=liblatalloc.so $CMD
```

Build and install it the usual way:

```sh
$ ./configure --optimize
$ make
$ sudo make install
```
