#include <os/string.h>

void memcpy(uint8_t *dest, const uint8_t *src, uint32_t len)
{
    for (; len != 0; len--) {
        *dest++ = *src++;
    }
}

void memset(void *dest, uint8_t val, uint32_t len)
{
    uint8_t *dst = (uint8_t *)dest;

    for (; len != 0; len--) {
        *dst++ = val;
    }
}

void bzero(void *dest, uint32_t len)
{
    memset(dest, 0, len);
}

int strlen(const char *src)
{
    int i = 0;
    while (src[i] != '\0') {
        i++;
    }
    return i;
}

int strcmp(const char *str1, const char *str2)
{
    while (*str1 && *str2) {
        if (*str1 != *str2) {
            return (*str1) - (*str2);
        }
        ++str1;
        ++str2;
    }
    return (*str1) - (*str2);
}

int strncmp(const char *str1, const char *str2, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        if (str1[i] == '\0' || str1[i] != str2[i])
            return str1[i] - str2[i];
    return 0;
}

char *strcpy(char *dest, const char *src)
{
    char *tmp = dest;

    while (*src) {
        *dest++ = *src++;
    }

    *dest = '\0';

    return tmp;
}

char *strncpy(char *dest, const char *src, int n)
{
    char *tmp = dest;

    while (*src && n-- > 0) {
        *dest++ = *src++;
    }

    while (n-- > 0) {
        *dest++ = '\0';
    }

    return tmp;
}

char *strcat(char *dest, const char *src)
{
    char *tmp = dest;

    while (*dest != '\0') {
        dest++;
    }
    while (*src) {
        *dest++ = *src++;
    }

    *dest = '\0';

    return tmp;
}

char *strtok(char *str, const char *delim) {
    static char *next; // 保存上一次调用的上下文
    if (str != NULL) {
        next = str; // 如果传入了新的字符串，初始化 next
    }
    if (next == NULL) {
        return NULL; // 如果没有更多的字符串可分割，返回 NULL
    }

    // 跳过前导的分隔符
    char *start = next;
    while (*start != '\0') {
        int is_delim = 0;
        for (int i = 0; delim[i] != '\0'; i++) {
            if (*start == delim[i]) {
                is_delim = 1;
                break;
            }
        }
        if (!is_delim) {
            break; // 找到第一个非分隔符字符
        }
        start++;
    }

    if (*start == '\0') {
        next = NULL; // 如果到达字符串末尾，返回 NULL
        return NULL;
    }

    // 找到下一个分隔符
    char *end = start;
    while (*end != '\0') {
        int is_delim = 0;
        for (int i = 0; delim[i] != '\0'; i++) {
            if (*end == delim[i]) {
                is_delim = 1;
                break;
            }
        }
        if (is_delim) {
            break; // 找到分隔符
        }
        end++;
    }

    if (*end == '\0') {
        next = NULL; // 如果到达字符串末尾，更新 next 为 NULL
    } else {
        *end = '\0'; // 将分隔符替换为 '\0'，分割字符串
        next = end + 1; // 更新 next 为下一个子字符串的起点
    }

    return start; // 返回当前子字符串
}

char *strrchr(const char *str, int c) {
    const char *last = NULL; // 用于记录最后一次出现的位置

    while (*str) {
        if (*str == (char)c) {
            last = str; // 更新最后一次出现的位置
        }
        str++;
    }

    return (char *)last; // 返回最后一次出现的位置，或者 NULL
}