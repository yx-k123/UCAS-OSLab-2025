#include <os/mm.h>

static ptr_t kernMemCurr = FREEMEM_KERNEL;
static ptr_t userMemCurr = FREEMEM_USER;

ptr_t allocKernelPage(int numPage)
{
    // align PAGE_SIZE
    ptr_t ret = ROUND(kernMemCurr, PAGE_SIZE);
    kernMemCurr = ret + numPage * PAGE_SIZE;
    return ret;
}

ptr_t allocUserPage(int numPage)
{
    // align PAGE_SIZE
    ptr_t ret = ROUND(userMemCurr, PAGE_SIZE);
    userMemCurr = ret + numPage * PAGE_SIZE;
    return ret;
}

void freeKernelPage(ptr_t addr, int num)
{
    ptr_t expected_addr = kernMemCurr - num * PAGE_SIZE;
    if (addr == expected_addr) {
        kernMemCurr = addr; 
    }
}

void freeUserPage(ptr_t addr, int num)
{
    ptr_t expected_addr = userMemCurr - num * PAGE_SIZE;
    if (addr == expected_addr) {
        userMemCurr = addr; 
    }
}
