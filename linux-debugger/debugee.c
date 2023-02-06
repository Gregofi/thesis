#include <stdio.h>

int foo() {
    __asm__("int3");
    printf("Exiting foo\n");
    return 0;
}

int main() {
    __asm__("int3");
    printf("Hello world!\n");
    foo();
    printf("After foo\n");
    printf("Goodbye world!\n");
}
