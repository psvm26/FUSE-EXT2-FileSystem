#ifndef _TYPES_H_
#define _TYPES_H_

/* Type def */
typedef int          boolean;
typedef enum newfs_file_type {
    REG_FILE,
    DIR,
} NEWFS_FILE_TYPE;

/* Macro */
#define TRUE                    1
#define FALSE                   0
#define UINT8_BITS              8

#define MAX_NAME_LEN            128  
#define SUPER_BLKS_NUM          1
#define INODE_MAP_BLKS_NUM      1   
#define DATA_MAP_BLKS_NUM       1   
#define MAX_INODE_BLKS_NUM      256
#define MAX_INODE_NUM_PERBLK    16
#define MAX_DATA_BLKS_NUM       3837
#define MAX_DATA_PERFILE        6 


#define NEWFS_MAGIC_NUM           0x20110520 
#define NEWFS_SUPER_OFS           0 
#define NEWFS_ROOT_INO            0

#define NEWFS_ERROR_ACCESS        EACCES
#define NEWFS_ERROR_SEEK          ESPIPE     
#define NEWFS_ERROR_ISDIR         EISDIR
#define NEWFS_ERROR_NOTDIR        ENOTDIR
#define NEWFS_ERROR_NOSPACE       ENOSPC
#define NEWFS_ERROR_EXISTS        EEXIST
#define NEWFS_ERROR_NOTFOUND      ENOENT
#define NEWFS_ERROR_UNSUPPORTED   ENXIO
#define NEWFS_ERROR_IO            EIO     /* Error Input/Output */
#define NEWFS_ERROR_INVAL         EINVAL

#define NEWFS_ERROR_NONE        0

/* Macro Function */
#define NEWFS_IO_SIZE()                     (super.io_size)
#define NEWFS_BLK_SIZE()                    (super.blks_size)
#define NEWFS_DISK_SIZE()                   (super.disk_size)
#define NEWFS_BLKS_SIZE(blks)               ((blks) * NEWFS_BLK_SIZE())
#define NEWFS_ROUND_DOWN(value, round)      ((value) % (round) == 0 ? (value) : ((value) / (round)) * (round))
#define NEWFS_ROUND_UP(value, round)        ((value) % (round) == 0 ? (value) : ((value) / (round) + 1) * (round))
#define NEWFS_ASSIGN_FNAME(pnewfs_dentry, _fname) memcpy(pnewfs_dentry->name, _fname, strlen(_fname))
#define MAX_DENTRY_PERBLK()                 (NEWFS_BLK_SIZE() / sizeof(struct newfs_dentry))
//计算偏移
#define NEWFS_INO_OFS(ino)                  (super.ino_offset + ((ino) /  MAX_INODE_NUM_PERBLK) * NEWFS_BLK_SIZE() + ((ino) %  MAX_INODE_NUM_PERBLK) * sizeof(struct newfs_inode_d))
#define NEWFS_DB_OFS(dno)                   (super.db_offset + NEWFS_BLKS_SIZE(dno))
//判断节点类型
#define NEWFS_IS_DIR(pinode)                (pinode->dentry->ftype == DIR)
#define NEWFS_IS_REG(pinode)                (pinode->dentry->ftype == REG_FILE)

/* macro debug */
#define NEWFS_DBG(fmt, ...) do { printf("NEWFS_DBG: " fmt, ##__VA_ARGS__); } while(0) 

/* in memory struction */
struct custom_options {
	const char*        device;
};

struct newfs_super {
    uint32_t magic;
    int      fd;
    /* TODO: Define yourself */
    int disk_size;          // 磁盘大小
    /* 逻辑块信息 */
    int io_size;            // io块大小
    int blks_size;          // 逻辑块大小
    int blks_nums;          // 逻辑块数

    /* 磁盘布局分区信息 */
    int sb_offset;          // 超级块于磁盘中的偏移 0
    int sb_blks;            // 超级块于磁盘中的块数 1

    int ino_map_offset;     // 索引节点位图于磁盘中的偏移 1
    int ino_map_blks;       // 索引节点位图于磁盘中的块数 1
    uint8_t* map_inode;

    int db_map_offset;     // 数据块位图于磁盘中的偏移 2
    int db_map_blks;       // 数据块位图于磁盘中的块数 1
    uint8_t* map_db;

    int ino_offset;         // 索引节点于磁盘中的偏移 3
    int ino_blks;           // 索引节点于磁盘中的块数 256

    int db_offset;          // 数据块于磁盘中的偏移 259
    int db_blks;            // 数据块于磁盘中的块数 3837

    /* 支持的限制 */
    int ino_max;            // 最大支持inode数
    int file_max;           // 支持文件最大大小

    /* 根目录索引 */
    int root_ino;           // 根目录对应的inode

    /* 其他信息 */
    boolean            is_mounted;
    int sz_usage;
    struct newfs_dentry* root_dentry;     // 根目录
};

struct newfs_inode {
    uint32_t ino;
    /* TODO: Define yourself */
    /* 文件的属性 */
    int                size;               // 文件已占用空间
    int                link;               // 链接数，默认为1
    NEWFS_FILE_TYPE    ftype;              // 文件类型（目录类型、普通文件类型）

    /* 数据块的索引 */
    int                block_pointer[MAX_DATA_PERFILE];   // 数据块指针（可固定分配）
    uint8_t*           data[MAX_DATA_PERFILE];

    /* 其他字段 */
    struct newfs_dentry* dentry;            // 指向该inode的dentry(父)
    struct newfs_dentry* dentrys;           // 指向的目录项
    int                dir_cnt;             // 如果是目录类型文件，下面有几个目录项 
};

struct newfs_dentry {
    /* 文件名 */
    char     name[MAX_NAME_LEN];
    /* inode编号 */
    uint32_t ino;
    /* TODO: Define yourself */
    /* 文件类型 */
    NEWFS_FILE_TYPE     ftype; 

    /* 其他 */
    struct newfs_inode* inode;
    struct newfs_dentry* parent;
    struct newfs_dentry* brother;
};

static inline struct newfs_dentry* new_dentry(char * fname, NEWFS_FILE_TYPE ftype) {
    struct newfs_dentry * dentry = (struct newfs_dentry *)malloc(sizeof(struct newfs_dentry));
    memset(dentry, 0, sizeof(struct newfs_dentry));
    NEWFS_ASSIGN_FNAME(dentry, fname);
    dentry->ftype   = ftype;
    dentry->ino     = -1;
    dentry->inode   = NULL;
    dentry->parent  = NULL;
    dentry->brother = NULL;     
    return dentry;                                       
}

/* to disk struction */
struct newfs_super_d {
    uint32_t magic;

    /* 磁盘布局分区信息 */
    int sb_offset;          // 超级块于磁盘中的偏移 0
    int sb_blks;            // 超级块于磁盘中的块数 1

    int ino_map_offset;     // 索引节点位图于磁盘中的偏移 1
    int ino_map_blks;       // 索引节点位图于磁盘中的块数 1

    int db_map_offset;     // 数据块位图于磁盘中的偏移 2
    int db_map_blks;       // 数据块位图于磁盘中的块数 1

    int ino_offset;         // 索引节点于磁盘中的偏移 3
    int ino_blks;           // 索引节点于磁盘中的块数 256

    int db_offset;          // 数据块于磁盘中的偏移 259
    int db_blks;            // 数据块于磁盘中的块数 3837

    /* 支持的限制 */
    int ino_max;            // 最大支持inode数
    int file_max;           // 支持文件最大大小

    /* 根目录索引 */
    int root_ino;           // 根目录对应的inode

    /* 其他信息 */
    int sz_usage;
};

struct newfs_inode_d {
    uint32_t ino;
    /* 文件的属性 */
    int                size;               // 文件已占用空间
    int                link;               // 链接数，默认为1
    NEWFS_FILE_TYPE    ftype;              // 文件类型（目录类型、普通文件类型）

    /* 数据块的索引 */
    int                block_pointer[MAX_DATA_PERFILE];   // 数据块指针（可固定分配）

    /* 其他字段 */
    int                dir_cnt;            // 如果是目录类型文件，下面有几个目录项 
};

struct newfs_dentry_d {
    /* 文件名 */
    char     name[MAX_NAME_LEN];
    /* inode编号 */
    uint32_t ino;
    /* 文件类型 */
    NEWFS_FILE_TYPE     ftype; 
};

#endif /* _TYPES_H_ */