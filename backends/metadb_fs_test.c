#include "operations.h"
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>

#define MAX_FILENAME_LEN 1024
#define MAX_NUM_ENTRIES  1024

int num_print_entries;
char* entry_list[MAX_NUM_ENTRIES];

static
void print_meta_obj_key(metadb_key_t *mkey) {
    printf("%ld, %ld, ", mkey->parent_id, mkey->partition_id);
    int i;
    for (i = 0; i < HASH_LEN; ++i)
        printf("%c", mkey->name_hash[i]);
    printf("\n");
}

static
void print_entries(void* buf, metadb_key_t* iter_key, metadb_obj_t* iter_obj) {
    if (entry_list[num_print_entries] != NULL) {
        memcpy(entry_list[num_print_entries], iter_key, sizeof(metadb_key_t));
    }
    ++num_print_entries;

    if (iter_obj != NULL && buf == NULL)
        print_meta_obj_key(iter_key);
}

static
void init_meta_obj_key(metadb_key_t *mkey,
                       metadb_inode_t dir_id,
                       int partition_id, const char* path)
{
    mkey->parent_id = dir_id;
    mkey->partition_id = partition_id;
    memset(mkey->name_hash, 0, sizeof(mkey->name_hash));
    giga_hash_name(path, mkey->name_hash);
}

void run_test() {
    char dbname[] = "/tmp/testdb";
    char dbname2[] = "/tmp/testdb2";
    struct MetaDB mdb;
    struct MetaDB mdb2;

    metadb_init(&mdb, dbname);
    metadb_init(&mdb2, dbname2);

    int dir_id = 0;
    int partition_id = 0;
    int new_partition_id = 1;

    char filename[MAX_FILENAME_LEN];
    char backup[MAX_FILENAME_LEN];
    struct stat statbuf;
    int num_migrated_entries = 0;
    metadb_inode_t i = 0;

    snprintf(filename, MAX_FILENAME_LEN, "%08x", 10000);
    metadb_test_put_and_get(mdb, dir_id, partition_id, filename);
    metadb_lookup(mdb, dir_id, partition_id, filename, &statbuf);

    for (i = 0; i < 50; ++i) {
        memset(filename, 0, sizeof(filename));
        snprintf(filename, MAX_FILENAME_LEN, "%16lx", i);
        memset(backup, 0, sizeof(filename));
        snprintf(backup, MAX_FILENAME_LEN, "%16lx", i);

        assert(metadb_create(mdb, dir_id, partition_id, OBJ_DIR, i,
                             filename, filename) == 0);
        metadb_lookup(mdb, dir_id, partition_id, filename, &statbuf);
        assert(statbuf.st_ino == i);
        if (giga_file_migration_status(backup, new_partition_id)) {
            ++num_migrated_entries;
            printf("%ld\n", i);
        }
    }

    printf("moved entries: %d \n", num_migrated_entries);

    char extname[] = "/tmp/extract";
    uint64_t min_seq, max_seq;
    int ret =
        metadb_extract(mdb, dir_id, partition_id, new_partition_id,
                       extname, &min_seq, &max_seq);

    assert(num_migrated_entries == ret);

    assert(metadb_bulkinsert(mdb2, extname, min_seq, max_seq) == 0);

    num_print_entries = 0;
    int k = 0;
    for (k = 0; k < MAX_NUM_ENTRIES; ++k)
        entry_list[k] = (char *) malloc(sizeof(metadb_key_t));

    assert(metadb_readdir(mdb2, dir_id, new_partition_id, NULL, print_entries) == 0);
    assert(num_migrated_entries == num_print_entries);

    num_print_entries = 0;
    assert(metadb_readdir(mdb2, dir_id, new_partition_id, NULL, print_entries) == 0);
    assert(num_migrated_entries == num_print_entries);

    printf("\n\n");

    int num_found_entries = 0;
    metadb_key_t testkey;
    init_meta_obj_key(&testkey, dir_id, partition_id, filename);

    for (i = 0; i < 50; ++i) {
        memset(filename, 0, sizeof(filename));
        snprintf(filename, MAX_FILENAME_LEN, "%16lx", i);

        init_meta_obj_key(&testkey, dir_id, new_partition_id, filename);
        printf("Num %02ld: ", i);
        print_meta_obj_key(&testkey);

        ret = metadb_lookup(mdb2, dir_id, new_partition_id, filename, &statbuf);
        if (ret == 0) {
            assert(statbuf.st_ino == i);
            ++num_found_entries;
        }
    }

    for (k = 0; k < MAX_NUM_ENTRIES; ++k)
        free(entry_list[k]);
    printf("%d %d\n", num_migrated_entries, num_found_entries);
    assert(num_migrated_entries == num_found_entries);

    metadb_close(mdb);
    metadb_close(mdb2);
}

int main() {
    run_test();
    return 0;
}