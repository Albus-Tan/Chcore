#include <string.h>
#include <stdio.h>

#include "file_ops.h"
#include "block_layer.h"

#define ENTRY_SIZE 32
#define ENTRY_NUM BLOCK_SIZE/ENTRY_SIZE
#define NAME_BLOCK_IDX 0
#define FIRST_BLOCK_IDX NAME_BLOCK_IDX+1
#define END_LABEL '$'

// Use the first block to store name and block index
// name$

static int next_idx = FIRST_BLOCK_IDX;

int get_file_block(const char *name)
{
        // read first block which store name
        char buf[BLOCK_SIZE];
        sd_bread(NAME_BLOCK_IDX, buf);
        size_t len = strlen(name);

        // traverse all entry
        for(int i = 0; i < ENTRY_NUM; ++i){
                // start from i * ENTRY_SIZE
                int j;
                for(j = 0; j < len; ++j){
                        if(buf[i*ENTRY_SIZE + j] != name[j] || buf[i*ENTRY_SIZE + j] == END_LABEL){
                                // check next entry
                                break;
                        }
                }
                if(j == len && buf[i*ENTRY_SIZE + j] == END_LABEL){
                        // has entry
                        // printf("entry %d match\n", i);
                        return i;
                }
                // printf("entry %d unmatch\n", i);
        }
        return -1;
}

int naive_fs_access(const char *name)
{
    /* LAB 6 TODO BEGIN */
    /* BLANK BEGIN */

    // read first block which store name
    char buf[BLOCK_SIZE];
    sd_bread(NAME_BLOCK_IDX, buf);
    size_t len = strlen(name);

    // traverse all entry
    for(int i = 0; i < ENTRY_NUM; ++i){
            // start from i * ENTRY_SIZE
            int j;
            for(j = 0; j < len; ++j){
                    if(buf[i*ENTRY_SIZE + j] != name[j] || buf[i*ENTRY_SIZE + j] == END_LABEL){
                            // check next entry
                            break;
                    }
            }
            if(j == len && buf[i*ENTRY_SIZE + j] == END_LABEL){
                    // has entry
                    // printf("entry %d match\n", i);
                    return 0;
            }
            // printf("entry %d unmatch\n", i);
    }
    return -1;
    /* BLANK END */
    /* LAB 6 TODO END */
    return -2;
}

int naive_fs_creat(const char *name)
{
    /* LAB 6 TODO BEGIN */
    /* BLANK BEGIN */
    if(naive_fs_access(name) == 0){
            return -1;
    }

    // read first block which store name
    char buf[BLOCK_SIZE];
    sd_bread(NAME_BLOCK_IDX, buf);
    size_t len = strlen(name);

    // alloc block
    int idx = next_idx;
    ++next_idx;

    // add name into first block
    strcpy(buf + idx*ENTRY_SIZE, name);
    buf[idx*ENTRY_SIZE + len] = END_LABEL;

    // printf("file named %s added\n", name);
    // printf("buf: %s\n", buf);

    // update block which store name
    sd_bwrite(NAME_BLOCK_IDX, buf);

    return 0;

    /* BLANK END */
    /* LAB 6 TODO END */
    return -2;
}

int naive_fs_pread(const char *name, int offset, int size, char *buffer)
{
    /* LAB 6 TODO BEGIN */
    /* BLANK BEGIN */
    int blk = get_file_block(name);
    if(blk == -1){
            return -1;
    }

    // read file block
    char buf[BLOCK_SIZE];
    sd_bread(blk, buf);

    memcpy(buffer, buf+offset, size);

    return size;

    /* BLANK END */
    /* LAB 6 TODO END */
    return -2;
}

int naive_fs_pwrite(const char *name, int offset, int size, const char *buffer)
{
    /* LAB 6 TODO BEGIN */
    /* BLANK BEGIN */
    int blk = get_file_block(name);
    if(blk == -1){
            return -1;
    }

    // read file block
    char buf[BLOCK_SIZE];
    sd_bread(blk, buf);

    memcpy(buf+offset, buffer, size);

    // write file block
    sd_bwrite(blk, buf);

    return size;

    /* BLANK END */
    /* LAB 6 TODO END */
    return -2;
}

int naive_fs_unlink(const char *name)
{
    /* LAB 6 TODO BEGIN */
    /* BLANK BEGIN */
    if(naive_fs_access(name) != 0){
            return -1;
    }

    /* BLANK END */
    /* LAB 6 TODO END */
    return -2;
}
