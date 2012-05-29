
#include "common/cache.h"
#include "common/connection.h"
#include "common/defaults.h"
#include "common/debugging.h"
#include "common/rpc_giga.h"
#include "common/options.h"

#include "backends/operations.h"

#include "server.h"
#include "split.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>

#define HANDLER_LOG LOG_DEBUG

bool_t giga_rpc_init_1_svc(int rpc_req, 
                           giga_result_t *rpc_reply, 
                           struct svc_req *rqstp)
{
    (void)rqstp;
    assert(rpc_reply);

    logMessage(HANDLER_LOG, __func__, "==> RPC_init_recv = %d", rpc_req);

    // send bitmap for the "root" directory.
    //
    int dir_id = 0;
    struct giga_directory *dir = cache_fetch(&dir_id);
    if (dir == NULL) {
        rpc_reply->errnum = -EIO;
        logMessage(HANDLER_LOG, __func__, "Dir (id=%d) not in cache!", dir_id);
        return true;
    }
    rpc_reply->errnum = -EAGAIN;
    memcpy(&(rpc_reply->giga_result_t_u.bitmap), 
           &dir->mapping, sizeof(dir->mapping));

    logMessage(HANDLER_LOG, __func__, "RPC_init_reply(%d)", rpc_reply->errnum);

    return true;
}

int giga_rpc_prog_1_freeresult(SVCXPRT *transp, 
                               xdrproc_t xdr_result, caddr_t result)
{
    (void)transp;
    
    logMessage(HANDLER_LOG, __func__, "RPC_freeresult_recv");

    xdr_free(xdr_result, result);

    /* TODO: add more cleanup code. */
    
    logMessage(HANDLER_LOG, __func__, "RPC_freeresult_reply");

    return 1;
}

bool_t giga_rpc_getattr_1_svc(giga_dir_id dir_id, giga_pathname path, 
                              giga_getattr_reply_t *rpc_reply, 
                              struct svc_req *rqstp)
{
    (void)rqstp;
    assert(rpc_reply);
    assert(path);

    logMessage(HANDLER_LOG, __func__, 
               "==> RPC_getattr_recv(dir_id=%d,path=%s)", dir_id, path);

    bzero(rpc_reply, sizeof(giga_getattr_reply_t));

    struct giga_directory *dir = cache_fetch(&dir_id);
    if (dir == NULL) {
        rpc_reply->result.errnum = -EIO;
        logMessage(HANDLER_LOG, __func__, "Dir (id=%d) not in cache!", dir_id);
        return true;
    }

    // (1): get the giga index/partition for operation
    int index = giga_get_index_for_file(&dir->mapping, (const char*)path);
    int server = giga_get_server_for_index(&dir->mapping, index);
    
    // (2): is this the correct server? NO --> (errnum=-EAGAIN) and return
    if (server != giga_options_t.serverID) {
        rpc_reply->result.errnum = -EAGAIN;
        memcpy(&(rpc_reply->result.giga_result_t_u.bitmap), 
               &dir->mapping, sizeof(dir->mapping));
        logMessage(HANDLER_LOG, __func__, "req for server-%d reached server-%d.",
                   server, giga_options_t.serverID);
        return true;
    }

    logMessage(HANDLER_LOG, __func__, "index=%d,server=%d", index, server);

    char path_name[MAX_LEN];

    switch (giga_options_t.backend_type) {
        case BACKEND_RPC_LOCALFS:
            snprintf(path_name, sizeof(path_name), 
                     "%s/%d/%s", giga_options_t.mountpoint, index, path);
            rpc_reply->result.errnum = local_getattr(path_name, 
                                                     &rpc_reply->statbuf);
            break;
        case BACKEND_RPC_LEVELDB:
            rpc_reply->result.errnum = metadb_lookup(ldb_mds,
                                                     dir_id, index, path,
                                                     &rpc_reply->statbuf);
            break;  
        default:
            break;

    }

    logMessage(HANDLER_LOG, __func__, "RPC_getattr_reply");
    return true;
}

bool_t giga_rpc_mkdir_1_svc(giga_dir_id dir_id, giga_pathname path, mode_t mode,
                            giga_result_t *rpc_reply, 
                            struct svc_req *rqstp)
{
    (void)rqstp;
    assert(rpc_reply);
    assert(path);

    logMessage(HANDLER_LOG, __func__, 
               "==> RPC_mkdir_recv(path=%s,mode=0%3o)", path, mode);

    bzero(rpc_reply, sizeof(giga_result_t));

    struct giga_directory *dir = cache_fetch(&dir_id);
    if (dir == NULL) {
        rpc_reply->errnum = -EIO;
        logMessage(HANDLER_LOG, __func__, "Dir (id=%d) not in cache!", dir_id);
        return true;
    }

    // (1): get the giga index/partition for operation
    int index = giga_get_index_for_file(&dir->mapping, (const char*)path);
    int server = giga_get_server_for_index(&dir->mapping, index);
    
    // (2): is this the correct server? NO --> (errnum=-EAGAIN) and return
    if (server != giga_options_t.serverID) {
        rpc_reply->errnum = -EAGAIN;
        memcpy(&(rpc_reply->giga_result_t_u.bitmap), 
               &dir->mapping, sizeof(dir->mapping));
        logMessage(HANDLER_LOG, __func__, "req for server-%d reached server-%d.",
                   server, giga_options_t.serverID);
        return true;
    }

    char path_name[MAX_LEN];

    switch (giga_options_t.backend_type) {
        case BACKEND_RPC_LOCALFS:
            snprintf(path_name, sizeof(path_name), 
                     "%s/%d/%s", giga_options_t.mountpoint, index, path);
            rpc_reply->errnum = local_mkdir(path_name, mode);
            break;
        case BACKEND_RPC_LEVELDB:
            // create object in the underlying file system
            // TODO: assume partitioned sub-dirs, and randomly pick a sub-dir
            //       for symlink creation (for PanFS)
            snprintf(path_name, sizeof(path_name), 
                     "%s/%s", giga_options_t.mountpoint, path);
            rpc_reply->errnum = local_mkdir(path_name, mode); 
            
            // create object entry (metadata) in levelDB
            object_id += 1; 
            rpc_reply->errnum = metadb_create(ldb_mds, dir_id, index,
                                              OBJ_DIR,
                                              object_id, path, path_name);
            break;
        default:
            break;

    }

    logMessage(HANDLER_LOG, __func__, 
               "RPC_mkdir_reply(status=%d)", rpc_reply->errnum);

    return true;
}


bool_t giga_rpc_mknod_1_svc(giga_dir_id dir_id, 
                            giga_pathname path, mode_t mode, short dev,
                            giga_result_t *rpc_reply, 
                            struct svc_req *rqstp)
{
    (void)rqstp;
    assert(rpc_reply);
    assert(path);

    logMessage(HANDLER_LOG, __func__, 
               "==> RPC_mknod_recv(path=%s,mode=0%3o)", path, mode);

    bzero(rpc_reply, sizeof(giga_result_t));

    struct giga_directory *dir = cache_fetch(&dir_id);
    if (dir == NULL) {
        rpc_reply->errnum = -EIO;
        logMessage(HANDLER_LOG, __func__, "Dir (id=%d) not in cache!", dir_id);
        return true;
    }

    // (1): get the giga index/partition for operation
    int index = giga_get_index_for_file(&dir->mapping, (const char*)path);
    int server = giga_get_server_for_index(&dir->mapping, index);
    
    // (2): is this the correct server? NO --> (errnum=-EAGAIN) and return
    if (server != giga_options_t.serverID) {
        rpc_reply->errnum = -EAGAIN;
        memcpy(&(rpc_reply->giga_result_t_u.bitmap), 
               &dir->mapping, sizeof(dir->mapping));
        logMessage(HANDLER_LOG, __func__, "req for server-%d reached server-%d.",
                   server, giga_options_t.serverID);
        return true;
    }

    // (3): check for splits.
    if (dir->partition_size[index] > SPLIT_THRESHOLD) {
        logMessage(HANDLER_LOG, __func__, 
                   "bucket(%d) has %d entries!.", index, dir->partition_size[index]);
        
        if ((rpc_reply->errnum = split_bucket(dir, index)) < 0) {
            logMessage(LOG_FATAL, __func__, "split failed.!!!");
            exit(1);    //FIXME: do somethign smarter
        }

        memcpy(&(rpc_reply->giga_result_t_u.bitmap), 
               &dir->mapping, sizeof(dir->mapping));

        rpc_reply->errnum = -EAGAIN;
    
        logMessage(HANDLER_LOG, __func__, 
                   "RPC_mknod_reply(status=%d) after split.", rpc_reply->errnum);
        
        return true;
    }
    

    char path_name[MAX_LEN];

    switch (giga_options_t.backend_type) {
        case BACKEND_RPC_LOCALFS:
            snprintf(path_name, sizeof(path_name), 
                     "%s/%d/%s", giga_options_t.mountpoint, index, path);
            rpc_reply->errnum = local_mknod(path_name, mode, dev);
            
            dir->partition_size[index] += 1;
            break;
        case BACKEND_RPC_LEVELDB:
            // create object in the underlying file system
            // TODO: assume partitioned sub-dirs, and randomly pick a dir
            //       for symlink creation (for PanFS)
            snprintf(path_name, sizeof(path_name), 
                     "%s/%s", giga_options_t.mountpoint, path);
            rpc_reply->errnum = local_mknod(path_name, mode, dev); 
           
            // create object entry (metadata) in levelDB
            // object_id += 1; 
            rpc_reply->errnum = metadb_create(ldb_mds, dir_id, index,
                                              OBJ_MKNOD,
                                              object_id, path, path_name);
            dir->partition_size[index] += 1;
            break;
        default:
            break;

    }

    logMessage(HANDLER_LOG, __func__, 
               "RPC_mknod_reply(status=%d)", rpc_reply->errnum);

    return true;
}

