#include <stdio.h>
#include <string.h>
#include "table.h"

/* Add 1 to the counter of `label`, creating the row if there is room. */
void table_add(struct table *t, const char *label)
{
	int i;

	/* Linear search: fine for TABLE_MAX = 32 rows. */
	for (i = 0; i < t->n; i++) {
		if (strncmp(t->rows[i].label, label, LABEL_LEN) == 0) {
			t->rows[i].count++;
			return;
		}
	}
	if (t->n == TABLE_MAX) {
		t->other++;
		return;
	}
	snprintf(t->rows[t->n].label, LABEL_LEN, "%s", label);
	t->rows[t->n].count = 1;
	t->n++;
}

void table_print(const struct table *t, const char *title)
{
	int i;

	printf("%s\n", title);
	for (i = 0; i < t->n; i++)
		printf("  %-*s %llu\n", LABEL_LEN, t->rows[i].label, t->rows[i].count);
	if (t->other)
		printf("  %-*s %llu\n", LABEL_LEN, "other", t->other);
}
