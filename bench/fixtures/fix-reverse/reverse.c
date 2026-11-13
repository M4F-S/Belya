#include <stdio.h>
#include <string.h>

void reverse(char *s) {
    int n = (int)strlen(s);
    for (int i = 0; i < n / 2; i++) {
        char t = s[i];
        s[i] = s[n - i];
        s[n - i] = t;
    }
}

int main(void) {
    char buf[] = "abc";
    reverse(buf);
    printf("%s\n", buf);
    return 0;
}
