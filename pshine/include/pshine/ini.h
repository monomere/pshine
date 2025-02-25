#ifndef PSHINE_INI_H_
#define PSHINE_INI_H_
#include <stddef.h>

struct pshine_ini_table {
	char *name;
	size_t entry_count;
	struct pshine_ini_entry *entries_own;
};

struct pshine_ini_entry {
	char *name;
	char *value;
};

struct pshine_ini_file {
	bool ok;
	size_t table_count;
	struct pshine_ini_table *tables_own;
	char *data;
};

struct pshine_ini_file pshine_read_ini_file(const char *fpath);
void pshine_free_ini_file(struct pshine_ini_file *f);
struct pshine_ini_table *pshine_find_table(struct pshine_ini_file *file, const char *name);
struct pshine_ini_entry *pshine_find_entry(struct pshine_ini_table *tab, const char *name);

#endif // PSHINE_INI_H_

