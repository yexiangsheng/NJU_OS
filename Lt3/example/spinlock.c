enum {
  NO = 0,
  YES = 1
};

int table = NO;

void lock()
{
retry:
  int got = xchg(&table, NO);
  if (got == NO)
    goto retry;
  assert(got == YES);
}

void unlock()
{
  xchg(&table, YES);
}
