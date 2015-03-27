#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>
#include <string>
#include <cstdio>


#include "pocl.h"
#include "pocl_file_util.h"
#include "pocl_cache.h"

#include <llvm/Support/LockFileManager.h>
#include <llvm/Support/Errc.h>
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_os_ostream.h"

#include "llvm/Bitcode/ReaderWriter.h"

#include "PoclLockFileManager.h"

#define RETURN_IF_EC if (ec) return ec.default_error_condition().value()

using namespace llvm;

/*****************************************************************************/

int
pocl_rm_rf(const char* path) {
    std::error_code ec;
    SmallString<128> DirNative;

    sys::path::native(Twine(path), DirNative);

    std::vector<std::string> FileSet, DirSet;

    for (sys::fs::recursive_directory_iterator Dir(DirNative.str(), ec), DirEnd;
         Dir != DirEnd && !ec; Dir.increment(ec)) {
        Twine p = Dir->path();
        std::string s = p.str();
        if (sys::fs::is_directory(p)) {
            DirSet.push_back(s);
        } else
            FileSet.push_back(s);
    }
    RETURN_IF_EC;

    std::vector<std::string>::iterator it;
    for (it = FileSet.begin(); it != FileSet.end(); ++it) {
        ec = sys::fs::remove(*it);
        RETURN_IF_EC;
    }

    std::sort(DirSet.begin(), DirSet.end());
    std::vector<std::string>::reverse_iterator it2;
    for (it2 = DirSet.rbegin(); it2 != DirSet.rend(); ++it2) {
        ec = sys::fs::remove(*it2);
        RETURN_IF_EC;
    }

    return 0;
}


int
pocl_mkdir_p(const char* path) {
    Twine p(path);
    std::error_code ec = sys::fs::create_directories(p, true);
    return ec.default_error_condition().value();
}

int
pocl_remove(const char* path) {
    Twine p(path);
    std::error_code ec = sys::fs::remove(p, true);
    return ec.default_error_condition().value();
}

int
pocl_exists(const char* path) {
    Twine p(path);
    if (sys::fs::exists(p))
        return 1;
    else
        return 0;
}

int
pocl_filesize(const char* path, uint64_t* res) {
    Twine p(path);
    std::error_code ec = sys::fs::file_size(p, *res);
    return ec.default_error_condition().value();
}


/****************************************************************************/

int
pocl_read_file(const char* path, char** content, uint64_t *filesize) {
    PoclLockFileManager lfm(path);
    *content = NULL;

    int errcode = pocl_filesize(path, filesize);
    if (!errcode) {
        // +1 so we can later simply turn it into a C string, if needed
        *content = (char*)malloc(*filesize+1);
        errcode = lfm.read_file(*content, *filesize);
    }
    return errcode;
}


int
pocl_write_file(const char* path,
                const char* content,
                uint64_t    count,
                int         append,
                int         dont_rewrite) {
    PoclLockFileManager lfm(path);
    return lfm.write_file(content, count, append, dont_rewrite);
}


int pocl_touch_file(const char* path) {
    PoclLockFileManager lfm(path);
    return lfm.touch_file();
}


int pocl_write_module(void *module, const char* path, int dont_rewrite) {
    PoclLockFileManager lfm(path);
    return lfm.write_module((llvm::Module*)module, dont_rewrite);
}


int pocl_remove_locked(const char* path) {
    PoclLockFileManager lfm(path);
    return lfm.remove_file();
}

/****************************************************************************/

static void* acquire_lock_internal(const char* path, int immediate) {
    PoclLockFileManager* lfm = new PoclLockFileManager(path, immediate);
    if (!lfm) {
        delete lfm;
        return NULL;
    } else
        return (void*)lfm;
}

void* acquire_lock(const char *path) {
    return acquire_lock_internal(path, 0);
}

void* acquire_lock_immediate(const char *path) {
    return acquire_lock_internal(path, 1);
}

void* acquire_lock_check_file_exists(const char* path, int* file_exists) {
    void* ret = acquire_lock(path);
    if (!ret)
        return NULL;
    *file_exists = ((PoclLockFileManager*)ret)->file_exists();
    return ret;
}

void release_lock(void* lock, int mark_as_done) {
    if (!lock)
        return;
    PoclLockFileManager *l = (PoclLockFileManager*)lock;
    if (mark_as_done)
        l->done();
    delete l;
}
