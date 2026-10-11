/*
 * port/platform/dump_zip.c
 *
 * F12's zip (dump_zip.h), written with miniz at its default compression.
 */
#include "dump_zip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_fs.h"
#include "miniz.h"

static void say(char *why, size_t why_size, const char *what, const char *detail)
{
    if (why != NULL && why_size > 0) {
        snprintf(why, why_size, "%s%s%s", what, detail != NULL ? ": " : "",
                 detail != NULL ? detail : "");
    }
}

/* the file's bytes in a malloc block (NULL when it cannot be read) */
static void *read_all(const char *path, size_t *size)
{
    FILE *f = ico_fopen(path, "rb");
    unsigned char *buf = NULL;
    size_t cap = 0, n = 0;

    if (f == NULL) {
        return NULL;
    }
    /* read until the end rather than trusting a size: the log is still
       growing and a character device or link has none */
    for (;;) {
        if (n == cap) {
            const size_t ncap = cap == 0 ? 65536 : cap * 2;
            unsigned char *nb = realloc(buf, ncap);

            if (nb == NULL) {
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = nb;
            cap = ncap;
        }
        const size_t got = fread(buf + n, 1, cap - n, f);

        n += got;
        if (got == 0) {
            break;
        }
    }
    const int bad = ferror(f);

    fclose(f);
    if (bad) {
        free(buf);
        return NULL;
    }
    *size = n;
    return buf;
}

static const char *base_name(const char *path)
{
    const char *b = path;

    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            b = p + 1;
        }
    }
    return b;
}

int ico_frame_dump_zip(const char *zip_path, const char *dump_path, const char *log_path, char *why,
                       size_t why_size)
{
    mz_zip_archive z;
    void *out = NULL;
    size_t out_size = 0, dump_size = 0, log_size = 0;
    void *dump = read_all(dump_path, &dump_size);
    void *log = log_path != NULL && log_path[0] != '\0' ? read_all(log_path, &log_size) : NULL;
    int r = -1;

    memset(&z, 0, sizeof(z));
    if (dump == NULL) {
        say(why, why_size, "cannot read the dump", dump_path);
    } else if (!mz_zip_writer_init_heap(&z, 0, 0)) {
        say(why, why_size, "cannot start the archive",
            mz_zip_get_error_string(mz_zip_get_last_error(&z)));
    } else {
        int ok =
            mz_zip_writer_add_mem(&z, base_name(dump_path), dump, dump_size, MZ_DEFAULT_LEVEL) != 0;

        if (ok && log != NULL) {
            ok = mz_zip_writer_add_mem(&z, base_name(log_path), log, log_size, MZ_DEFAULT_LEVEL) !=
                 0;
        }
        if (!ok) {
            say(why, why_size, "cannot add to the archive",
                mz_zip_get_error_string(mz_zip_get_last_error(&z)));
            mz_zip_writer_end(&z);
        } else if (!mz_zip_writer_finalize_heap_archive(&z, &out, &out_size)) {
            say(why, why_size, "cannot finish the archive",
                mz_zip_get_error_string(mz_zip_get_last_error(&z)));
            mz_zip_writer_end(&z);
        } else {
            mz_zip_writer_end(&z);
            FILE *f = ico_fopen(zip_path, "wb");
            const int wrote = f != NULL && fwrite(out, 1, out_size, f) == out_size;

            if (f != NULL && fclose(f) != 0) {
                r = -1;
            } else if (wrote) {
                r = log != NULL ? 0 : 1;
            }
            if (r < 0) {
                say(why, why_size, "cannot write", zip_path);
                remove(zip_path);
            } else {
                remove(dump_path);
            }
            mz_free(out);
        }
    }
    free(dump);
    free(log);
    return r;
}

void *ico_dump_zip_extract(const char *zip_path, size_t *size, char *name, size_t name_size)
{
    mz_zip_archive z;
    void *data = NULL, *zip = NULL;
    size_t zip_size = 0;

    zip = read_all(zip_path, &zip_size);
    if (zip == NULL) {
        return NULL;
    }
    memset(&z, 0, sizeof(z));
    if (!mz_zip_reader_init_mem(&z, zip, zip_size, 0)) {
        free(zip);
        return NULL;
    }
    const mz_uint n = mz_zip_reader_get_num_files(&z);

    for (mz_uint i = 0; i < n && data == NULL; i++) {
        mz_zip_archive_file_stat st;

        if (!mz_zip_reader_file_stat(&z, i, &st) || st.m_is_directory) {
            continue;
        }
        const size_t len = strlen(st.m_filename);

        if (len < 7 || strcmp(st.m_filename + len - 7, ".rddump") != 0) {
            continue;
        }
        data = mz_zip_reader_extract_to_heap(&z, i, size, 0);
        if (data != NULL && name != NULL && name_size > 0) {
            snprintf(name, name_size, "%s", base_name(st.m_filename));
        }
    }
    mz_zip_reader_end(&z);
    free(zip);
    return data;
}

static const char *const kSweepName[ICO_SWEEP_COUNT] = {"4x3", "16x9", "32x9", "48x9", "60x9"};

const char *ico_sweep_aspect_name(int i)
{
    return i >= 0 && i < ICO_SWEEP_COUNT ? kSweepName[i] : "";
}

float ico_sweep_aspect_value(int i)
{
    static const float v[ICO_SWEEP_COUNT] = {4.0f / 3.0f, 16.0f / 9.0f, 32.0f / 9.0f, 48.0f / 9.0f,
                                             60.0f / 9.0f};

    return i >= 0 && i < ICO_SWEEP_COUNT ? v[i] : 0.0f;
}

int ico_sweep_zip(const char *zip_path, const char *prefix, char *why, size_t why_size)
{
    mz_zip_archive z;
    void *out = NULL;
    size_t out_size = 0;
    char path[2][4096], entry[2][32];
    int have[ICO_SWEEP_COUNT] = {0}, added = 0, r = -1;
    static const char *const kExt[2] = {"rddump", "png"};

    memset(&z, 0, sizeof(z));
    if (!mz_zip_writer_init_heap(&z, 0, 0)) {
        say(why, why_size, "cannot start the archive",
            mz_zip_get_error_string(mz_zip_get_last_error(&z)));
        return -1;
    }
    for (int i = 0; i < ICO_SWEEP_COUNT; i++) {
        void *data[2] = {NULL, NULL};
        size_t size[2] = {0, 0};
        int ok = 1;

        for (int k = 0; k < 2; k++) {
            snprintf(path[k], sizeof(path[k]), "%s%s.%s", prefix, kSweepName[i], kExt[k]);
            snprintf(entry[k], sizeof(entry[k]), "%s.%s", kSweepName[i], kExt[k]);
            data[k] = read_all(path[k], &size[k]);
            ok = ok && data[k] != NULL;
        }
        for (int k = 0; ok && k < 2; k++) {
            ok = mz_zip_writer_add_mem(&z, entry[k], data[k], size[k], MZ_DEFAULT_LEVEL) != 0;
        }
        free(data[0]);
        free(data[1]);
        if (ok) {
            have[i] = 1;
            added++;
        } else if (data[0] != NULL || data[1] != NULL) {
            /* one file of the pair is missing: that aspect is left out (a
               member half added cannot be taken back, so a failed add ends
               the archive) */
            if (mz_zip_get_last_error(&z) != MZ_ZIP_NO_ERROR) {
                say(why, why_size, "cannot add to the archive",
                    mz_zip_get_error_string(mz_zip_get_last_error(&z)));
                mz_zip_writer_end(&z);
                return -1;
            }
        }
    }
    if (added == 0) {
        say(why, why_size, "no aspect has both its dump and its picture", prefix);
        mz_zip_writer_end(&z);
        return -1;
    }
    if (!mz_zip_writer_finalize_heap_archive(&z, &out, &out_size)) {
        say(why, why_size, "cannot finish the archive",
            mz_zip_get_error_string(mz_zip_get_last_error(&z)));
        mz_zip_writer_end(&z);
        return -1;
    }
    mz_zip_writer_end(&z);
    FILE *f = ico_fopen(zip_path, "wb");
    const int wrote = f != NULL && fwrite(out, 1, out_size, f) == out_size;

    if (f != NULL && fclose(f) != 0) {
        r = -1;
    } else if (wrote) {
        r = added;
    }
    if (r < 0) {
        say(why, why_size, "cannot write", zip_path);
        remove(zip_path);
    } else {
        for (int i = 0; i < ICO_SWEEP_COUNT; i++) {
            for (int k = 0; have[i] && k < 2; k++) {
                snprintf(path[k], sizeof(path[k]), "%s%s.%s", prefix, kSweepName[i], kExt[k]);
                remove(path[k]);
            }
        }
    }
    mz_free(out);
    return r;
}
