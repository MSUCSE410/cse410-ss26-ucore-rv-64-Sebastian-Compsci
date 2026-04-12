#ifndef __STAT_H__
#define __STAT_H__

#include "types.h"

struct Stat { 
    uint64 dev; // drive number of the disk where the file is located, this implementation is written to 0. 
    uint64 ino; // inode The inode number of the file. 
    uint32 mode; // file type 
    uint32 nlink; // number of hard links, initially 1 
    uint64 pad[7]; // no need to consider, designed for compatibility 
};

#endif