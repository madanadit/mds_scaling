#include "common/connection.h"
#include "common/debugging.h"
#include "common/defaults.h"

#include "operations.h"

#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <assert.h>

#define METADB_LOG LOG_DEBUG

#define DEFAULT_LEVELDB_CACHE_SIZE 100000
#define DEFAULT_WRITE_BUFFER_SIZE  100000
#define DEFAULT_MAX_OPEN_FILES     128
#define DEFAULT_SSTABLE_SIZE 2*1024*1024
#define MAX_FILENAME_LEN 1024
#define DEFAULT_INTERNAL_KEY_LEN (sizeof(metadb_key_t) + sizeof(uint64_t))

#define METADB_KEY_LEN (sizeof(metadb_key_t))

#define metadb_error(phase, cond)                                        \
  if (cond != NULL) {                                                    \
    fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, phase, cond); \
    abort();                                                             \
  }

static
void init_meta_obj_key(metadb_key_t *mkey,
                       metadb_inode_t dir_id, int partition_id, const char* path)
{
    sprintf(mkey->ids, "%016llx%016llx", dir_id, (uint64_t) partition_id);
    memset(mkey->name_hash, 0, sizeof(mkey->name_hash));
    snprintf(mkey->name_hash, HASH_LEN, "%s", path);
}

static
void print_meta_obj_key(metadb_key_t *mkey) {
    printf("%s\n",  mkey->name_hash);
}

static
void init_meta_obj_seek_key(metadb_key_t *mkey,
                            metadb_inode_t dir_id, int partition_id)
{
    sprintf(mkey->ids, "%016llx%016llx", dir_id, (uint64_t) partition_id);
    memset(mkey->name_hash, 0, sizeof(mkey->name_hash));
}

static
metadb_inode_t metadb_key_get_parent_id(metadb_key_t *mkey) {
    metadb_inode_t inode_id;
    sscanf(mkey->ids, "%16llx", &inode_id);
    return inode_id;
}

static
uint64_t metadb_key_get_partition_id(metadb_key_t *mkey) {
    uint64_t partition_id;
    sscanf(mkey->ids+16, "%16llx", &partition_id);
    return partition_id;
}

static
metadb_obj_t* create_metadb_obj(const char* objname, const size_t objname_len,
                                const char* realpath, const size_t realpath_len,
                                size_t *allocation)
{
    *allocation = (sizeof(metadb_obj_t) + realpath_len + objname_len + 1) / 8 * 8 + 9;
    char* val = (char *) malloc(*allocation);
    val[(*allocation)-1] = '\0';

    metadb_obj_t *mobj = (metadb_obj_t*) val;
    if (mobj != NULL) {
        mobj->objname_len = objname_len;
        mobj->realpath_len = realpath_len;

        mobj->objname = (char*)mobj + sizeof(metadb_obj_t);
        strncpy(mobj->objname, objname, objname_len);
        mobj->objname[objname_len] = '\0';

        mobj->realpath = (char*)mobj + sizeof(metadb_obj_t) + objname_len + 1;
        strncpy(mobj->realpath, realpath, realpath_len);
        mobj->realpath[realpath_len] = '\0';
    }
    return mobj;
}

static
size_t metadb_obj_size(const metadb_obj_t* mobj) {
    size_t mobj_size = sizeof(metadb_obj_t) +
                       (mobj->objname_len)  +
                       (mobj->realpath_len) + 2;
    return mobj_size;
}

static
void init_meta_obj_statbuf(metadb_obj_t* mobj,
                const metadb_inode_t inode_id,
                metadb_obj_type_t obj_type)
{

    //FIXME: Initialize Inode

    lstat("./", &(mobj->statbuf));
    mobj->statbuf.st_ino = inode_id;
    if (obj_type == OBJ_DIR) {
        mobj->statbuf.st_mode = (mobj->statbuf.st_mode & ~S_IFMT) | S_IFDIR;
    } else {
        mobj->statbuf.st_mode = (mobj->statbuf.st_mode & ~S_IFMT) | S_IFREG;
    }

    logMessage(METADB_LOG, __func__, "init_meta_obj: %16lx",
               mobj->statbuf.st_mode);

    time_t now = time(NULL);
    mobj->statbuf.st_atime = now;
    mobj->statbuf.st_mtime = now;
    mobj->statbuf.st_ctime = now;
}


int metadb_init(struct MetaDB *mdb, const char *mdb_name)
{
    char* err = NULL;

    mdb->env = leveldb_create_default_env();
    mdb->cache = leveldb_cache_create_lru(DEFAULT_LEVELDB_CACHE_SIZE);

    mdb->options = leveldb_options_create();
    leveldb_options_set_error_if_exists(mdb->options, 0);
    leveldb_options_set_cache(mdb->options, mdb->cache);
    leveldb_options_set_env(mdb->options, mdb->env);
    leveldb_options_set_info_log(mdb->options, NULL);
    leveldb_options_set_write_buffer_size(mdb->options,
                                          DEFAULT_WRITE_BUFFER_SIZE);
    leveldb_options_set_paranoid_checks(mdb->options, 0);
    leveldb_options_set_max_open_files(mdb->options, DEFAULT_MAX_OPEN_FILES);
    leveldb_options_set_create_if_missing(mdb->options, 1);
    leveldb_options_set_compression(mdb->options, leveldb_no_compression);

    mdb->lookup_options = leveldb_readoptions_create();
    leveldb_readoptions_set_fill_cache(mdb->lookup_options, 1);

    mdb->scan_options = leveldb_readoptions_create();
    leveldb_readoptions_set_fill_cache(mdb->scan_options, 0);

    mdb->insert_options = leveldb_writeoptions_create();
    leveldb_writeoptions_set_sync(mdb->insert_options, 0);

    mdb->db = leveldb_open(mdb->options, mdb_name, &err);
    metadb_error("leveldb_init", err);

    logMessage(METADB_LOG, __func__, "LevelDB table(%s)(%ld) created.", mdb_name, strlen(mdb_name));

    return 0;
}


static int create_count = 0;

int metadb_create(struct MetaDB mdb,
                  const metadb_inode_t dir_id, const int partition_id,
                  metadb_obj_type_t entry_type,
                  const metadb_inode_t inode_id, const char *path,
                  const char *realpath)
{
    int ret = 0;
    char* err = NULL;

    logMessage(METADB_LOG, __func__, "create %ld %d %d %ld %ld %ld %ld",
           dir_id, partition_id, entry_type, inode_id, path, realpath, sizeof(mdb));

<<<<<<< HEAD
            create_count += 1;

            char val[5] = {'a'};
            size_t vlen = 5;
            char key[32] = {'a'};
            sprintf(key, "test%016d", create_count);
            size_t klen = 32;

            leveldb_writeoptions_t* insert_options = leveldb_writeoptions_create();
            leveldb_writeoptions_set_sync(insert_options, 0);

            leveldb_put(mdb.db, insert_options,
                        key, klen,
                        val, vlen, &err);

            leveldb_writeoptions_destroy(insert_options);

=======
    create_count += 1;

    char val[5] = {'a'};
    size_t vlen = 5;
    char key[32] = {'a'};
    sprintf(key, "test%016d", create_count);
    size_t klen = 32;

    leveldb_put(mdb.db, mdb.insert_options,
                key, klen,
                val, vlen, &err);
>>>>>>> f927a9ad6a3253bac08c0b64ef76ae601cb99c67

    if (err != NULL) {
        logMessage(METADB_LOG, __func__, "create(%s): err (%s)",
           path, err);
        ret = -1;
    }

    return ret;
}
int metadb_lookup(struct MetaDB mdb,
                  const metadb_inode_t dir_id, const int partition_id,
                  const char *path, struct stat *stbuf)
{
    int ret = 0;

    logMessage(METADB_LOG, __func__, "lookup(%ld) in (partition=%d,dirid=%ld,size=%ld)",
               path, partition_id, dir_id, sizeof(mdb));

    char realpath[200];
    snprintf(realpath, 200, "/tmp/giga_s/%s", path);
    if (lstat(realpath, stbuf) < 0) {
        ret = ENOENT;
    }

    stbuf->st_mode = (stbuf->st_mode & ~S_IFMT) | S_IFREG;

    return ret;
}

int metadb_close(struct MetaDB mdb) {
  printf("%d\n", sizeof(mdb));
  leveldb_close(mdb.db);
  leveldb_options_destroy(mdb.options);
  leveldb_cache_destroy(mdb.cache);
  leveldb_env_destroy(mdb.env);
  leveldb_readoptions_destroy(mdb.lookup_options);
  leveldb_readoptions_destroy(mdb.scan_options);
  leveldb_writeoptions_destroy(mdb.insert_options);
  return 0;
}

int metadb_remove(struct MetaDB mdb,
                  const metadb_inode_t dir_id,
                  const int partition_id,
                  const char *path) {
    metadb_key_t mobj_key;
    char* err = NULL;

    logMessage(METADB_LOG, __func__, "metadb_remove(%s) in (partition=%d,dirid=%ld)",
               path, partition_id, dir_id);

    init_meta_obj_key(&mobj_key, dir_id, partition_id, path);
    leveldb_delete(mdb.db, mdb.insert_options,
            (char *) &mobj_key, METADB_KEY_LEN,
            &err);
    if (err == NULL) {
        return 0;
    } else {
        return -1;
    }
}

int metadb_readdir(struct MetaDB mdb,
                   const metadb_inode_t dir_id,
                   const int partition_id,
                   void *buf, fill_dir_t filler) {

    int ret = 0;
    metadb_key_t mobj_key;

    logMessage(METADB_LOG, __func__, "metadb_readdir in (partition=%d,dirid=%ld)",
               partition_id, dir_id);

    init_meta_obj_seek_key(&mobj_key, dir_id, partition_id);
    leveldb_iterator_t* iter = leveldb_create_iterator(mdb.db, mdb.scan_options);
    leveldb_iter_seek(iter, (char *) &mobj_key, METADB_KEY_LEN);
    if (leveldb_iter_valid(iter)) {
        do {
            metadb_key_t* iter_key;
            metadb_obj_t* iter_obj;
            size_t len;
            iter_key = (metadb_key_t*) leveldb_iter_key(iter, &len);
            if (metadb_key_get_parent_id(iter_key) == dir_id &&
                metadb_key_get_partition_id(iter_key) == (uint64_t) partition_id) {
                iter_obj = (metadb_obj_t*) leveldb_iter_value(iter, &len);
                filler(buf, iter_key, iter_obj);
            } else {
                break;
            }
            leveldb_iter_next(iter);
        } while (leveldb_iter_valid(iter));
    } else {
        printf("metadb_readdir: Invalid Iterator.\n");
        ret = ENOENT;
    }
    leveldb_iter_destroy(iter);
    return ret;
}
/*
static void build_sstable_filename(const char* dir_with_new_partition,
                                   int new_partition_id,
                                   int num_new_sstable,
                                   char* sstable_filename) {
  snprintf(sstable_filename, MAX_FILENAME_LEN,
           "%s/p%d-%08x.sst", dir_with_new_partition,
           new_partition_id, num_new_sstable);
}

static void construct_new_key(const char* old_key,
                              int key_len,
                              int new_partition_id,
                              char* new_key) {
    memcpy(new_key, old_key, key_len);
    metadb_key_t* user_key = (metadb_key_t*) new_key;
    sprintf(user_key->ids+16, "%016lx", (uint64_t) new_partition_id);
    user_key->partition_id = new_partition_id;
}

static uint64_t get_sequence_number(const char* key,
                                    int key_len) {
    uint64_t num;
    //FIXME: Current code only considers little-endian
    memcpy(&num, (key + key_len - 8), sizeof(num));
    return num >> 8;
}
*/
static int directory_exists(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return 1;
    } else {
        return 0;
    }
}

int metadb_extract(struct MetaDB mdb,
                   const metadb_inode_t dir_id,
                   const int old_partition_id,
                   const int new_partition_id,
                   const char* dir_with_new_partition,
                   uint64_t* min_sequence_number,
                   uint64_t* max_sequence_number)
{

    int ret = 0;
//    char* err = NULL;

    logMessage(METADB_LOG, __func__,
               "metadb_extract from (partition=%d,dirid=%ld) to (partition=%d) %ld with minseq(%ld), maxseq(%ld) %s",
               old_partition_id, dir_id, new_partition_id, sizeof(mdb),
               min_sequence_number, max_sequence_number, dir_with_new_partition);


    if (!directory_exists(dir_with_new_partition)) {
        mkdir(dir_with_new_partition, DEFAULT_MODE);
    }

    metadb_key_t mobj_key;
    init_meta_obj_seek_key(&mobj_key, dir_id, old_partition_id);

    // steps for splitting: P_i (old partition id) into P_j (new partition id)
    //
    // init:
    // -- create new_sstable_File
    //
    // scan the table for all keys "in" P_i and for all keys do:
    // -- move_status giga_file_migration_status(filename, P_j)
    // -- if (move_status == 0)
    //        do not move entry
    //    else if (move_status == 1)
    //        (1) delete old entry
    //        (2) add old entry into new_sst_file
    //
    // return:
    // -- path for new_sst_file

    /*
    int num_new_sstable = 0;
    int num_migrated_entries = 0;
    char sstable_filename[MAX_FILENAME_LEN];
    char new_internal_key[DEFAULT_INTERNAL_KEY_LEN];
    build_sstable_filename(dir_with_new_partition,
                           new_partition_id, num_new_sstable,
                           sstable_filename);
    leveldb_tablebuilder_t* builder = leveldb_tablebuilder_create(
        mdb.options, sstable_filename, mdb.env, &err);
    metadb_error("create new builder", err);

    leveldb_iterator_t* iter =
      leveldb_create_iterator(mdb.db, mdb.scan_options);
    leveldb_writebatch_t* batch = leveldb_writebatch_create();

    if (!leveldb_iter_valid(iter)) {

        uint64_t min_seq, max_seq;
        leveldb_iter_seek(iter, (char *) &mobj_key, METADB_KEY_LEN);

        while (leveldb_iter_valid(iter)) {
            size_t klen;
            const char* iter_ori_key = leveldb_iter_key(iter, &klen);
            metadb_key_t* iter_key = (metadb_key_t*) iter_ori_key;


            if (metadb_key_get_parent_id(iter_key) == dir_id &&
                metadb_key_get_partition_id(iter_key) == (uint64_t) old_partition_id) {

                size_t vlen;
                const char* iter_ori_val = leveldb_iter_value(iter, &vlen);
                const metadb_obj_t* iter_obj =
                    (const metadb_obj_t *) iter_ori_val;
                if (giga_file_migration_status_with_hash(iter_key->name_hash,
                                                         new_partition_id)) {

                    leveldb_writebatch_delete(batch, iter_ori_key, klen);

                    size_t iklen;
                    const char* iter_internal_key =
                        leveldb_iter_internalkey(iter, &iklen);
                    construct_new_key(iter_internal_key, iklen,
                        new_partition_id, new_internal_key);
                    leveldb_tablebuilder_put(builder,
                        new_internal_key, iklen, iter_ori_val, vlen);

                    uint64_t sequence_number =
                        get_sequence_number(iter_internal_key, iklen);
                    if (!num_migrated_entries) {
                        min_seq = sequence_number;
                        max_seq = sequence_number;
                    } else {
                        if (sequence_number < min_seq) {
                            min_seq = sequence_number;
                        } else if (sequence_number > max_seq) {
                            max_seq = sequence_number;
                        }
                    }

                    num_migrated_entries++;

                    metadb_key_t* new_key = (metadb_key_t *) new_internal_key;
                    printf("Default key len %ld %ld\n", DEFAULT_INTERNAL_KEY_LEN, METADB_KEY_LEN);
                    printf("metadb_extract %d: %s %s %ld %ld %ld\n", __LINE__,
                       iter_key->ids, new_key->ids,
                       klen, iter_obj->statbuf.st_ino, vlen);
                }

                if (leveldb_tablebuilder_size(builder) >= DEFAULT_SSTABLE_SIZE)
                {
                    // flush sstable file
                    leveldb_tablebuilder_destroy(builder);
                    // create new sstable builder
                    ++num_new_sstable;
                    build_sstable_filename(dir_with_new_partition,
                        new_partition_id, num_new_sstable,
                        sstable_filename);
                    builder = leveldb_tablebuilder_create(
                    mdb.options, sstable_filename, mdb.env, &err);
                    metadb_error("create new builder", err);
                    // delete moved entries
                    leveldb_write(mdb.db, mdb.insert_options, batch, &err);
                    metadb_error("delete moved entreis", err);
                    leveldb_writebatch_clear(batch);
                }
            } else {
                break;
            }
            leveldb_iter_next(iter);
        }
        *min_sequence_number = min_seq;
        *max_sequence_number = max_seq;
        ret = num_migrated_entries;
    } else {
        ret = ENOENT;
    }

    if (leveldb_tablebuilder_size(builder) > 0) {
        leveldb_write(mdb.db, mdb.insert_options, batch, &err);
        metadb_error("delete moved entreis", err);
    }
    leveldb_writebatch_destroy(batch);
    leveldb_tablebuilder_destroy(builder);
    leveldb_iter_destroy(iter);
    */

    return ret;
}

int metadb_bulkinsert(struct MetaDB mdb,
                      const char* dir_with_new_partition,
                      uint64_t min_sequence_number,
                      uint64_t max_sequence_number) {
    int ret = 0;
    //char sstable_filename[MAX_FILENAME_LEN];
    // char* err = NULL;

    logMessage(METADB_LOG, __func__, "metadb_bulkinsert from %s %ld",
               dir_with_new_partition, sizeof(mdb));

    if (min_sequence_number > max_sequence_number) {
      return ret;
    }

    /*
    DIR* dp = opendir(dir_with_new_partition);
    if (dp != NULL) {
        struct dirent *de;
        while ((de = readdir(dp)) != NULL) {
          if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
            snprintf(sstable_filename, MAX_FILENAME_LEN,
                     "%s/%s", dir_with_new_partition, de->d_name);

            leveldb_table_t* table = leveldb_table_create(
                mdb.options, sstable_filename, mdb.env, &err);
            metadb_error("create new builder", err);

            leveldb_iterator_t* iter =
              leveldb_table_create_iterator(table, mdb.scan_options);

            leveldb_iter_seek_to_first(iter);
            while (leveldb_iter_valid(iter)) {
                size_t klen, vlen;
                const char* key = leveldb_iter_key(iter, &klen);
                const char* val = leveldb_iter_value(iter, &vlen);
                const metadb_obj_t* iter_obj =
                    (const metadb_obj_t *) val;
                const metadb_key_t* iter_key =
                    (const metadb_key_t *) key;
                printf("metadb_bulkinsert %d: %s %ld %ld %ld\n", __LINE__,
                   iter_key->ids,
                   klen, iter_obj->statbuf.st_ino, vlen);
                leveldb_put(mdb.db, mdb.insert_options, key, klen-sizeof(uint64_t), val, vlen,
                            &err);
                printf("%ld %ld\n", klen, vlen);
                metadb_error("bulkinsert", err);
                leveldb_iter_next(iter);
            }

            leveldb_iter_destroy(iter);
            leveldb_table_destroy(table);
          }
        }
    }
    closedir(dp);
    */

    return ret;
}

/*
int metadb_bulkinsert_real(struct MetaDB mdb,
                      const char* dir_with_new_partition,
                      uint64_t min_sequence_number,
                      uint64_t max_sequence_number) {

    int ret = 0;
    char sstable_filename[MAX_FILENAME_LEN];
    char* err = NULL;

    DIR* dp = opendir(dir_with_new_partition);
    if (dp != NULL) {
        struct dirent *de;
        while ((de = readdir(dp)) != NULL) {
          if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
            snprintf(sstable_filename, MAX_FILENAME_LEN,
                     "%s/%s", dir_with_new_partition, de->d_name);
            leveldb_bulkinsert(mdb.db, mdb.insert_options,
                               sstable_filename,
                               min_sequence_number,
                               max_sequence_number,
                               &err);
            metadb_error("bulkinsert", err);
          }
        }
    }
    closedir(dp);
    return ret;
}
*/

void metadb_test_put_and_get(struct MetaDB mdb,
                             const metadb_inode_t dir_id,
                             const int partition_id,
                             const char *path) {
    char key[] = "test1";
    char val[] = "value1";
    char* err = NULL;

    leveldb_put(mdb.db, mdb.insert_options,
                key, strlen(key),
                val, strlen(val), &err);

    size_t new_val_len;
    char* new_val = leveldb_get(mdb.db, mdb.lookup_options,
                                key, strlen(key), &new_val_len, &err);
    printf("%s\n", new_val);

    int new_key = 12346;

    leveldb_put(mdb.db, mdb.insert_options,
                (char *) &new_key, sizeof(new_key),
                val, strlen(val), &err);

    new_val = leveldb_get(mdb.db, mdb.lookup_options,
                          (char *) &new_key, sizeof(new_key),
                          &new_val_len, &err);
    printf("%s\n", new_val);

    metadb_key_t mobj_key;
    init_meta_obj_key(&mobj_key, dir_id, partition_id, path);

    print_meta_obj_key(&mobj_key);


    char testval[] = "value2";
    leveldb_put(mdb.db, mdb.insert_options,
                (const char*) &mobj_key, METADB_KEY_LEN,
                testval, strlen(testval), &err);

    char* shit = leveldb_get(mdb.db, mdb.lookup_options,
                          (const char*) &mobj_key, METADB_KEY_LEN,
                          &new_val_len, &err);
//    printf("%s %ld\n", shit, new_val_len);

    metadb_obj_t mobj;
    mobj.objname_len = 0;
    mobj.realpath_len = 0;
    init_meta_obj_statbuf(&mobj, 0, 0);

    leveldb_put(mdb.db, mdb.insert_options,
                (const char*) &mobj_key, METADB_KEY_LEN,
                (const char*) &mobj, metadb_obj_size(&mobj), &err);

    shit = leveldb_get(mdb.db, mdb.lookup_options,
                          (const char*) &mobj_key, METADB_KEY_LEN,
                          &new_val_len, &err);
    metadb_obj_t* shitobj = (metadb_obj_t *) shit;
    metadb_obj_t* nmobj;
    size_t msize;
    nmobj = create_metadb_obj(path, strlen(path), path, strlen(path), &msize);
    init_meta_obj_statbuf(nmobj, 0, 0);

    leveldb_put(mdb.db, mdb.insert_options,
                (const char*) &mobj_key, METADB_KEY_LEN,
                (const char*) nmobj, msize, &err);

    shit = leveldb_get(mdb.db, mdb.lookup_options,
                       (const char*) &mobj_key, METADB_KEY_LEN,
                       &new_val_len, &err);
    shitobj = (metadb_obj_t *) shit;
}
