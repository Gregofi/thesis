#include <stdio.h>

int main() {
    __asm__("int3");
    printf("Hello world!\n");
    printf("Working!\n");
    printf("Goodbye world!\n");
}
