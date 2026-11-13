#include <stdio.h>

int helper(int x) { return x * 2; }

int main(void) {
    printf("%d\n", helper(21));
    return 0;
}
