#include "fsinfo.h"

#include <switch.h>

long fsinfoEntryCount(const char *folder)
{
    FsFileSystem *fs = NULL;
    char path[FS_MAX_PATH];
    if (fsdevTranslatePath(folder, &fs, path) < 0 || !fs)
        return -1;
    FsDir dir;
    if (R_FAILED(fsFsOpenDirectory(fs, path, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles |
                                                  FsDirOpenMode_NoFileSize, &dir)))
        return -1;
    s64 count = -1;
    if (R_FAILED(fsDirGetEntryCount(&dir, &count)))
        count = -1;
    fsDirClose(&dir);
    return (long)count;
}
