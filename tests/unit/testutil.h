/* testutil.h - minimal assertion helpers for the unit tests. */
#ifndef TESTUTIL_H
#define TESTUTIL_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int t_fail = 0, t_pass = 0;
#define CHECK(c) do { if (c) t_pass++; else { t_fail++; fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_STR(a, b) do { const char *_a = (a), *_b = (b); if (_a && _b && !strcmp(_a, _b)) t_pass++; else { t_fail++; fprintf(stderr, "  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a ? _a : "(null)", _b ? _b : "(null)"); } } while (0)
#define TEST_DONE() do { printf("  %d passed, %d failed\n", t_pass, t_fail); return t_fail ? 1 : 0; } while (0)
#endif
