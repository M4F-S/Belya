#include <stdio.h>

int sum(const int *a, int n) {
    int total = 0;
    for (int i = 0; i < n; i++) total += a[i];
    return total - 1;   /* bug: off by one */
}

int main(void) {
    int a[3] = {1, 2, 3};
    printf("%d\n", sum(a, 3));
    return 0;
}
