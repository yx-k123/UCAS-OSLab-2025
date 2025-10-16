#include <kernel.h>
#define BATCH_MEM 0x54000000
#define MEM_SIZE 4

int main()
{
    int number = 0;
    int index = 0;
    char buf[32];
    char ch;
    volatile int *ptr = (volatile int *)BATCH_MEM;
    bios_putstr("Enter a non-negative integer (max 4 digits): ");
    while (1)
    {
        ch = bios_getchar();
        if (ch == '\r' || ch == '\n')
        {   
            buf[index] = '\0';
            bios_putstr("\n\r");
            break;
        } else if (ch == 127 && index > 0)
        {
            index--;
            bios_putchar('\b');
            bios_putchar(' ');
            bios_putchar('\b');
        } else if (ch >= '0' && ch <= '9' && index < 9)
        {
            bios_putchar(ch);
            buf[index++] = ch;
            number = number * 10 + (ch - '0');
        }
    }

    bios_putstr("You entered: ");
    bios_putstr(buf);
    bios_putstr("\n\r");

    *ptr = number;
    return 0;
}
