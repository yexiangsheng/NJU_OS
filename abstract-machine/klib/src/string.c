#include <klib.h>
#include <klib-macros.h>
#include <stdint.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

size_t strlen(const char *s)
{
  // panic("Not implemented");

  size_t num = 0;
  while (*s != '\0')
  {
    num++;
    s++;
  }
  //  assert(*s != '\0');
  return num;
}

char *strcpy(char *dst, const char *src)
{
  size_t i = 0;
  while (src[i] != '\0')
  {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
  return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
  size_t i = 0;
  while (i < n && src[i] != '\0')
  {
    dst[i] = src[i];
    i++;
  }
  // src 比 n 短时剩余部分补 '\0'；src >= n 时不写终止符（C 标准语义）
  while (i < n)
  {
    dst[i] = '\0';
    i++;
  }
  return dst;
}

char *strcat(char *dst, const char *src)
{
  strcpy(dst + strlen(dst), src);
  return dst;
}

int strcmp(const char *s1, const char *s2)
{
  size_t i = 0;
  while (s1[i] != '\0' && s1[i] == s2[i])
  {
    i++;
  }
  // 按 unsigned char 比较，返回首个不同字节的差值符号
  return (unsigned char)s1[i] - (unsigned char)s2[i];
}

int strncmp(const char *s1, const char *s2, size_t n)
{
  size_t i = 0;
  while (i < n && s1[i] != '\0' && s1[i] == s2[i])
  {
    i++;
  }
  if (i == n)
  {
    return 0;
  }
  return (unsigned char)s1[i] - (unsigned char)s2[i];
}

void *memset(void *s, int c, size_t n)
{
  unsigned char *p = s;
  for (size_t i = 0; i < n; i++)
  {
    p[i] = (unsigned char)c;
  }
  return s;
}

void *memmove(void *dst, const void *src, size_t n)
{
  unsigned char *d = dst;
  const unsigned char *s = src;
  if (d < s)
  {
    // 不重叠或 dst 在前：正序拷贝安全
    for (size_t i = 0; i < n; i++)
    {
      d[i] = s[i];
    }
  }
  else if (d > s)
  {
    // dst 在后（可能重叠）：倒序拷贝，避免先覆盖尚未读取的字节
    for (size_t i = n; i > 0; i--)
    {
      d[i - 1] = s[i - 1];
    }
  }
  return dst;
}

void *memcpy(void *out, const void *in, size_t n)
{
  unsigned char *o = out;
  const unsigned char *i = in;
  for (size_t k = 0; k < n; k++)
  {
    o[k] = i[k];
  }
  return out;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
  const unsigned char *a = s1;
  const unsigned char *b = s2;
  for (size_t i = 0; i < n; i++)
  {
    if (a[i] != b[i])
    {
      return a[i] - b[i];
    }
  }
  return 0;
}

#endif
