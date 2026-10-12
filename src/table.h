/* Small fixed-size "label -> counter" table (per-process, per-destination). */
#ifndef TABLE_H
#define TABLE_H

/*
 * Label cardinality cap.
 * Each distinct label becomes one Prometheus time series, and Prometheus keeps
 * every series in memory. Process names and destination IPs are unbounded
 * (a port scan touches thousands of IPs), so we keep only the first TABLE_MAX
 * distinct labels and count everything else under a single "other" label.
 * We deliberately do NOT use a "current top N" ranking: a label that drops out
 * of the top N would make its series disappear and reappear, which breaks
 * rate() in Prometheus. With "first N + other", counters only ever go up.
 */
#define TABLE_MAX 32
#define LABEL_LEN 32

struct row {
	char label[LABEL_LEN];
	unsigned long long count;
};

struct table {
	struct row rows[TABLE_MAX];
	int n;                    /* rows in use */
	unsigned long long other; /* events whose label did not fit */
};

void table_add(struct table *t, const char *label);
void table_print(const struct table *t, const char *title);

#endif
