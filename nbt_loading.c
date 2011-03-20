/*
 * -----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Lukas Niederbremer <webmaster@flippeh.de> and Clark Gaebel <cg.wowus.cg@gmail.com>
 * wrote this file. As long as you retain this notice you can do whatever you
 * want with this stuff. If we meet some day, and you think this stuff is worth
 * it, you can buy us a beer in return.
 * -----------------------------------------------------------------------------
 */
#include "nbt.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

/* Works around a bug where it isn't forward declared in stdio.h */
extern int fileno(FILE*);

/* parses the whole file into a buffer */
static inline struct buffer __parse_level(gzFile fp)
{
    char buf[4096];
    size_t bytes_read;

    struct buffer ret = BUFFER_INIT;

    while((bytes_read = gzread(fp, buf, 4096)) > 0)
    {
        int err;
        gzerror(fp, &err);
        if(err)
        {
            errno = NBT_EGZ;
            goto parse_error;
        }

        if(buffer_append(&ret, buf, bytes_read))
        {
            errno = NBT_EMEM;
            goto parse_error;
        }
    }

    return ret;

parse_error:
    buffer_free(&ret);
    return BUFFER_INIT;
}

/*
 * No incremental parsing goes on. We just dump the whole decompressed file into
 * memory then pass the job off to nbt_parse.
 */
nbt_node* nbt_parse_level(FILE* fp)
{
    nbt_node* ret;

    errno = NBT_OK;

    /*
     * We need to keep these declarations up here as opposed to where they're
     * used because they're referenced by the parse_error block.
     */
    struct buffer buf = BUFFER_INIT;
    gzFile f = Z_NULL;

                           if(fp == NULL)         goto parse_error;
    int fd = fileno(fp);   if(fd == -1)           goto parse_error;
    f = gzdopen(fd, "rb"); if(f == Z_NULL)        goto parse_error;

    buf = __parse_level(f);

                           if(buf.data == NULL)   goto parse_error;
                           if(gzclose(f) != Z_OK) goto parse_error;

    ret = nbt_parse(buf.data, buf.len);

    buffer_free(&buf);
    return ret;

parse_error:
    if(errno == NBT_OK)
        errno = NBT_EGZ;

    buffer_free(&buf);

    if(f != Z_NULL)
        gzclose(f);

    return NULL;
}

/* See: http://mod.ifies.com/f/110216/region.c */
nbt_node* nbt_parse_chunk(const void* chunk_start, size_t length)
{
    struct buffer buf = BUFFER_INIT;

    /* the number of bytes we will process at a time */
    const size_t chunk_size = 4096;

    errno = NBT_OK;

    z_stream stream = {
        .zalloc   = Z_NULL,
        .zfree    = Z_NULL,
        .opaque   = Z_NULL,
        .next_in  = (void*)chunk_start,
        .avail_in = length
    };

    if(inflateInit(&stream) != Z_OK) goto parse_error;

    int zlib_ret;

    do {
        if(buffer_reserve(&buf, buf.len + chunk_size))
        {
            errno = NBT_EMEM;
            goto parse_error;
        }

        stream.avail_out = chunk_size;
        stream.next_out  = (unsigned char*)buf.data + buf.len;

        zlib_ret = inflate(&stream, Z_NO_FLUSH);
        buf.len += chunk_size - stream.avail_out;

        if(zlib_ret == Z_DATA_ERROR) goto parse_error;
        if(zlib_ret == Z_NEED_DICT)  goto parse_error;
        if(zlib_ret == Z_MEM_ERROR)
        {
            errno = NBT_EMEM;        goto parse_error;
        }

    } while(stream.avail_out == 0);

    /*
     * If we're at the end of the input data, we'd sure as hell be at the end
     * of the zlib stream.
     */
    if(zlib_ret != Z_STREAM_END)     goto parse_error;

    (void)inflateEnd(&stream);

    /* at this point, the whole decompressed data is in `buffer' */

    nbt_node* ret = nbt_parse(buf.data, buf.len);
    buffer_free(&buf);

    if(errno != NBT_OK)              return NULL;
    return ret;

parse_error:
    if(errno == NBT_OK)
        errno = NBT_EGZ;

    (void)inflateEnd(&stream);
    buffer_free(&buf);
    return NULL;
}

struct buffer nbt_dump_chunk(const nbt_node* tree)
{
    return BUFFER_INIT;
}
