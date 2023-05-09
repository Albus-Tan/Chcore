/*
 * Copyright (c) 2022 Institute of Parallel And Distributed Systems (IPADS)
 * ChCore-Lab is licensed under the Mulan PSL v1.
 * You can use this software according to the terms and conditions of the Mulan PSL v1.
 * You may obtain a copy of Mulan PSL v1 at:
 *     http://license.coscl.org.cn/MulanPSL
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v1 for more details.
 */

#include <stdio.h>
#include <string.h>
#include <chcore/types.h>
#include <chcore/fsm.h>
#include <chcore/tmpfs.h>
#include <chcore/ipc.h>
#include <chcore/internal/raw_syscall.h>
#include <chcore/internal/server_caps.h>
#include <chcore/procm.h>
#include <chcore/fs/defs.h>

typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, l)   __builtin_va_arg(v, l)
#define va_copy(d, s)  __builtin_va_copy(d, s)


extern struct ipc_struct *fs_ipc_struct;

/* You could add new functions or include headers here.*/
/* LAB 5 TODO BEGIN */

int alloc_FILE_fd()
{
        static int fs_cnt = 3;
        return ++fs_cnt;
}

int ipc_open_file(const char *filename, int file_fd, unsigned int file_mode) {
        struct ipc_msg *ipc_msg = ipc_create_msg(
                fs_ipc_struct, sizeof(struct fs_request), 0);
        chcore_assert(ipc_msg);
        struct fs_request * fr =
                (struct fs_request *)ipc_get_msg_data(ipc_msg);
        fr->req = FS_REQ_OPEN;
        strcpy(fr->open.pathname, filename);
        fr->open.flags = file_mode;
        fr->open.new_fd = file_fd;
        int ret = ipc_call(fs_ipc_struct, ipc_msg);
        ipc_destroy_msg(fs_ipc_struct, ipc_msg);
        return ret;
}

int ipc_create_file(const char *filename) {
        struct ipc_msg *ipc_msg = ipc_create_msg(
                fs_ipc_struct, sizeof(struct fs_request), 0);
        chcore_assert(ipc_msg);
        struct fs_request * fr =
                (struct fs_request *)ipc_get_msg_data(ipc_msg);
        fr->req = FS_REQ_CREAT;
        strcpy(fr->creat.pathname, filename);
        int ret = ipc_call(fs_ipc_struct, ipc_msg);
        ipc_destroy_msg(fs_ipc_struct, ipc_msg);
        return ret;
}

int ipc_read_file(int file_fd, char* buf, size_t len) {
        int cnt = 256;
        int ret = 0;
        int p = 0;
        do{
                struct ipc_msg *ipc_msg = ipc_create_msg(
                        fs_ipc_struct, sizeof(struct fs_request) + cnt + 2, 0);
                chcore_assert(ipc_msg);
                struct fs_request * fr =
                        (struct fs_request *)ipc_get_msg_data(ipc_msg);
                fr->req = FS_REQ_READ;
                fr->read.fd = file_fd;
                if(len > cnt){
                        fr->read.count = cnt;
                        len -= cnt;
                } else {
                        fr->read.count = len;
                        len = 0;
                }
                ret = ipc_call(fs_ipc_struct, ipc_msg);
                if(ret > 0) {
                        memcpy(buf + p, ipc_get_msg_data(ipc_msg), ret);
                        p += ret;
                }
                ipc_destroy_msg(fs_ipc_struct, ipc_msg);
        } while(ret > 0 && len != 0);
#ifdef FILE_DBG
        printf("read_file_from_tfs read %d bytes \n", ret);
#endif
        return ret;
}

int ipc_write_file(int file_fd, const char* buf, size_t len) {
        int cnt = 256;
        int ret = 0;
        int p = 0;
        do{
                struct ipc_msg *ipc_msg = ipc_create_msg(
                        fs_ipc_struct, sizeof(struct fs_request) + cnt, 0);
                chcore_assert(ipc_msg);
                struct fs_request * fr =
                        (struct fs_request *)ipc_get_msg_data(ipc_msg);
                fr->req = FS_REQ_WRITE;
                fr->write.fd = file_fd;
                if(len > cnt){
                        fr->write.count = cnt;
                        len -= cnt;
                } else {
                        fr->write.count = len;
                        len = 0;
                }
                memcpy((void *)fr + sizeof(struct fs_request), buf + p, fr->write.count);
                ret = ipc_call(fs_ipc_struct, ipc_msg);
                if(ret > 0) {
                        p += ret;
                }
                ipc_destroy_msg(fs_ipc_struct, ipc_msg);
        } while(ret > 0 && len != 0);
#ifdef FILE_DBG
        printf("read_file_from_tfs read %d bytes \n", ret);
#endif
        return ret;
}

int ipc_close_file(int file_fd) {
        struct ipc_msg *ipc_msg = ipc_create_msg(
                fs_ipc_struct, sizeof(struct fs_request), 0);
        chcore_assert(ipc_msg);
        struct fs_request * fr = (struct fs_request *)ipc_get_msg_data(ipc_msg);
        fr->req = FS_REQ_CLOSE;
        fr->close.fd = file_fd;
        int ret = ipc_call(fs_ipc_struct, ipc_msg);
        ipc_destroy_msg(fs_ipc_struct, ipc_msg);
        return ret;
}


void int2str(int n, char *str){
        char buf[MAX_FMT_SIZE];
        int i = 0, tmp = n;

        if(!str){
                return;
        }
        while(tmp){
                buf[i] = (char)(tmp % 10) + '0';
                tmp /= 10;
                i++;
        }
        int len = i;
        str[i] = '\0';
        while(i > 0){
                str[len - i] = buf[i - 1];
                i--;
        }
}

void str2int(char *str, int *n){
        int tmp = 0;
        for(int i = 0; i < strlen(str); i++){
                tmp = tmp * 10 + (str[i] - '0');
        }

        *n = tmp;
}

// return new buf_cur
size_t add_str_to_buf(char* buf, const char* str, size_t buf_cur) {
        size_t len = strlen(str);
        size_t cur;
        for(cur = 0; cur < len; ++cur){
                buf[buf_cur + cur] = str[cur];
        }
        return buf_cur + cur;
}

bool is_empty_space(char c){
        return (c == ' ' || c == '\t' || c == '\n');
}

// return new buf_cur
size_t get_str_from_buf(char* buf, char* str, size_t buf_cur) {
        size_t len = strlen(buf);
        size_t cur;
        char c;
        for(cur = 0; cur < len; ++cur){
                c = buf[buf_cur + cur];
#ifdef FILE_DBG
                printf("get_str_from_buf: c %c\n", c);
#endif
                if(is_empty_space(c)) {
                        ++cur;
                        break;
                }
                str[cur] = c;
        }
        str[cur + 1] = '\0';
#ifdef FILE_DBG
        printf("get_str_from_buf: finish, str %s\n", str);
#endif
        return buf_cur + cur;
}

// return new buf_cur
size_t add_int_to_buf(char* buf, int i, size_t buf_cur) {
        char str[MAX_FMT_SIZE];
        int2str(i, str);
        return add_str_to_buf(buf, str, buf_cur);
}

// return new buf_cur
size_t get_int_from_buf(char* buf, int* i, size_t buf_cur) {
        char str[MAX_FMT_SIZE];
        size_t sz = get_str_from_buf(buf, str, buf_cur);
        str2int(str, i);
        return sz;
}

/* LAB 5 TODO END */


FILE *fopen(const char * filename, const char * mode) {
	/* LAB 5 TODO BEGIN */
        int ret;
        int file_fd = alloc_FILE_fd();

        unsigned int file_mode;
        if(mode[0] == 'r' && mode[1] == '\0'){
                file_mode = O_RDONLY;
        } else if (mode[0] == 'w' && mode[1] == '\0'){
                file_mode = O_WRONLY;
        } else {
                file_mode = O_RDWR;
        }

        FILE *file = malloc(sizeof(struct FILE));

        file->fd = file_fd;
        file->mode = file_mode;

        ret = ipc_open_file(filename, file_fd, file_mode);
        if(ret < 0) {
#ifdef FILE_DBG
                printf("open file %s failed\n", filename);
                printf("create file %s\n", filename);
#endif
                ret = ipc_create_file(filename);
                if (ret < 0){
#ifdef FILE_DBG
                        printf("create file %s failed\n", filename);
#endif
                }
#ifdef FILE_DBG
                printf("reopen file %s\n", filename);
#endif
                ret = ipc_open_file(filename, file_fd, file_mode);
                if(ret >= 0){
#ifdef FILE_DBG
                        printf("reopen file %s success\n", filename);
#endif
                        return file;
                }
        } else {
#ifdef FILE_DBG
                printf("open file %s success\n", filename);
#endif
                return file;
        }
#ifdef FILE_DBG
        printf("reopen file %s failed\n", filename);
#endif
	/* LAB 5 TODO END */
        return NULL;
}

size_t fwrite(const void * src, size_t size, size_t nmemb, FILE * f) {

	/* LAB 5 TODO BEGIN */
        size_t ret;
        size_t len = size * nmemb;
        ret = ipc_write_file(f->fd, src, len);
        return ret;
	/* LAB 5 TODO END */

}

size_t fread(void * destv, size_t size, size_t nmemb, FILE * f) {

	/* LAB 5 TODO BEGIN */
        size_t ret;
        size_t len = size * nmemb;
        ret = ipc_read_file(f->fd, destv, len);
        return ret;
	/* LAB 5 TODO END */

}

int fclose(FILE *f) {
	/* LAB 5 TODO BEGIN */
        int ret;
        ret = ipc_close_file(f->fd);
        if(ret < 0){
#ifdef FILE_DBG
                printf("close file failed\n");
#endif
        }
        free(f);
	/* LAB 5 TODO END */
    return 0;

}

/* Need to support %s and %d. */
int fscanf(FILE * f, const char * fmt, ...) {
	/* LAB 5 TODO BEGIN */

#ifdef FILE_DBG
        printf("fscanf: fmt is %s\n", fmt);
#endif

        char buf[MAX_FMT_SIZE];
        memset(buf, 0x0, sizeof(buf));

        fread(buf, sizeof(char), sizeof(buf), f);

#ifdef FILE_DBG
        printf("fscanf buf is %s, fmt is %s\n", buf, fmt);
#endif

        va_list ap;
        va_start(ap, fmt);

        size_t size = strlen(fmt);
        size_t cur = 0;
        size_t buf_cur = 0;
        char c;
        while(cur < size){

                c = fmt[cur];
                ++cur;

                switch (c) {
                case '%':{
                        c = fmt[cur];
                        ++cur;
#ifdef FILE_DBG
                        printf("fscanf: fmt char %c\n", c);
#endif
                        switch (c) {
                        case 's':{
                                char* str = va_arg(ap, char*);
                                buf_cur = get_str_from_buf(buf, str, buf_cur);
#ifdef FILE_DBG
                                printf("fscanf: str arg get, %s\n", str);
#endif
                                break;
                        }
                        case 'd':{
                                int *dec = va_arg(ap, int*);
                                buf_cur = get_int_from_buf(buf, dec, buf_cur);
#ifdef FILE_DBG
                                printf("fscanf: int arg get, %d\n", *dec);
#endif
                                break;
                        }
                        default:{
                        }
                        }
                        break;
                }
                default:{

                }
                }
        }

        va_end(ap);

	/* LAB 5 TODO END */
        return 0;
}

/* Need to support %s and %d. */
int fprintf(FILE * f, const char * fmt, ...) {

	/* LAB 5 TODO BEGIN */

#ifdef FILE_DBG
        printf("fprintf: fmt is %s\n", fmt);
#endif

        char buf[MAX_FMT_SIZE];
        memset(buf, 0x0, sizeof(buf));

        va_list ap;
        va_start(ap, fmt);

        size_t size = strlen(fmt);
        size_t cur = 0;
        size_t buf_cur = 0;
        char c;
        while(cur < size){

                c = fmt[cur];
                ++cur;

                switch (c) {
                case '%':{
                        c = fmt[cur];
                        ++cur;
#ifdef FILE_DBG
                        printf("fprintf: fmt char %c\n", c);
#endif
                        switch (c) {
                        case 's':{
                                char* str = va_arg(ap, char*);
#ifdef FILE_DBG
                                printf("fprintf: str arg, %s\n", str);
#endif
                                buf_cur = add_str_to_buf(buf, str, buf_cur);
                                break;
                        }
                        case 'd':{
                                int dec = va_arg(ap, int);
#ifdef FILE_DBG
                                printf("fprintf: int arg, %d\n", dec);
#endif
                                buf_cur = add_int_to_buf(buf, dec, buf_cur);
                                break;
                        }
                        default:{
                                buf[buf_cur] = '%';
                                ++buf_cur;
                                buf[buf_cur] = c;
                                ++buf_cur;
                        }
                        }

                        break;
                }
                default:{
                        buf[buf_cur] = c;
                        ++buf_cur;
                }
                }
        }

        va_end(ap);

        buf[buf_cur] = '\0';

#ifdef FILE_DBG
        printf("fprintf final buf is %s, fmt is %s\n", buf, fmt);
#endif

        fwrite(buf, sizeof(char), buf_cur + 1, f);

	/* LAB 5 TODO END */
    return 0;
}

