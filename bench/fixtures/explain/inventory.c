#include <stdio.h>

typedef struct { char name[32]; int qty; } Item;

int main(void) {
    Item items[3] = {{"bolt", 12}, {"nut", 30}, {"washer", 7}};
    int total = 0;
    for (int i = 0; i < 3; i++) {
        printf("%s: %d\n", items[i].name, items[i].qty);
        total += items[i].qty;
    }
    printf("total: %d\n", total);
    return 0;
}
