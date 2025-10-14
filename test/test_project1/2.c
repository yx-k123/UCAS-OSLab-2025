#include <kernel.h>
#define BATCH_MEM 0x54000000
#define MEN_SIZE 32

static void int_to_str(int num, char *str)
{
    char *p = str;
    char *p1, *p2;
    int is_negative = 0;

    // Handle negative numbers
    if (num < 0) {
        is_negative = 1;
        num = -num;
    }

    // Convert integer to string
    do {
        *p++ = (num % 10) + '0';
        num /= 10;
    } while (num > 0);

    // Add negative sign if needed
    if (is_negative) {
        *p++ = '-';
    }

    *p = '\0';

    // Reverse the string
    p1 = str;
    p2 = p - 1;
    while (p1 < p2) {
        char temp = *p1;
        *p1 = *p2;
        *p2 = temp;
        p1++;
        p2--;
    }
}

int main()
{
    int number = 0;

    volatile int *ptr = (volatile int *)BATCH_MEM;
    number = *ptr;

    number += 10;
    char buf[32];
    int_to_str(number, buf);
    bios_putstr("Number after adding 10: ");
    bios_putstr(buf);
    bios_putstr("\n");

    *ptr = number;
    return 0;
}
