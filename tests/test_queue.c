/* Host-side tests for the queue shuffling. Run with: make test
 *
 * Shuffle is a Fisher-Yates over a slice of the play queue, seeded so it can be
 * reproduced in a test. The properties that matter: it is a permutation (no
 * song lost or duplicated), it only touches the slice it is given, and a given
 * seed always produces the same order.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

static int s_fails;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            s_fails++;                                                     \
        }                                                                  \
    } while (0)

static void fill(int *items, int n)
{
    for (int i = 0; i < n; i++)
        items[i] = i;
}

// Every value appears exactly once.
static void check_permutation(const int *items, int n)
{
    char seen[64];
    memset(seen, 0, sizeof(seen));
    for (int i = 0; i < n; i++) {
        if (items[i] < 0 || items[i] >= n) {
            printf("FAIL: %d at index %d is out of range\n", items[i], i);
            s_fails++;
            return;
        }
        if (seen[items[i]]) {
            printf("FAIL: %d appears twice\n", items[i]);
            s_fails++;
            return;
        }
        seen[items[i]] = 1;
    }
}

int main(void)
{
    int a[64], b[64];

    // Same seed, same order; a different seed usually differs.
    fill(a, 64);
    util_shuffle_range(a, 64, 12345u);
    check_permutation(a, 64);

    fill(b, 64);
    util_shuffle_range(b, 64, 12345u);
    CHECK(memcmp(a, b, sizeof(a)) == 0);

    int same = 1;
    for (int i = 0; i < 64; i++)
        if (a[i] != i)
            same = 0;
    CHECK(!same); // 64 elements left in order means the shuffle did nothing

    fill(b, 64);
    util_shuffle_range(b, 64, 999u);
    CHECK(memcmp(a, b, sizeof(a)) != 0);

    // Only the given slice moves: the head and tail stay put.
    fill(a, 16);
    util_shuffle_range(a + 4, 8, 7u);
    CHECK(a[0] == 0 && a[1] == 1 && a[2] == 2 && a[3] == 3);
    CHECK(a[12] == 12 && a[13] == 13 && a[14] == 14 && a[15] == 15);
    int moved = 0;
    for (int i = 4; i < 12; i++)
        if (a[i] != i)
            moved = 1;
    CHECK(moved);

    // A slice of 0 or 1 element, and a NULL pointer, are left alone.
    fill(a, 8);
    util_shuffle_range(a, 1, 5u);
    util_shuffle_range(a, 0, 5u);
    util_shuffle_range(NULL, 8, 5u);
    for (int i = 0; i < 8; i++)
        CHECK(a[i] == i);

    if (s_fails) {
        printf("queue tests: %d FAILED\n", s_fails);
        return 1;
    }
    printf("queue tests: all passed\n");
    return 0;
}
