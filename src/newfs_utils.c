#include "../include/newfs.h"

extern struct newfs_super super;
extern struct custom_options newfs_options;	

/**
 * @brief 获取文件名
 * 
 * @param path 
 * @return char* 
 */
char* newfs_get_fname(const char* path) {
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
}

int newfs_calc_lvl(const char * path) {
    char* str = path;
    int   lvl = 0;
    // path为'/'，根节点，lvl为0
    if (strcmp(path, "/") == 0) {
        return lvl;
    }
    // 层级等于'/'的个数
    while (*str != NULL) {
        if (*str == '/') {
            lvl++;
        }
        str++;
    }
    return lvl;
}

/**
 * @brief 驱动读
 * 
 * @param offset 
 * @param out_content 
 * @param size 
 * @return int 
 */
int newfs_driver_read(int offset, uint8_t *out_content, int size){
    // 按照一个逻辑块大小(1024B)封装
    int offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_BLK_SIZE());
    int bias = offset - offset_aligned;
    int size_aligned = NEWFS_ROUND_UP(size+bias, NEWFS_BLK_SIZE());
    uint8_t* temp_content = (uint8_t*)malloc(size_aligned);
    uint8_t* cur = temp_content;

    ddriver_seek(super.fd, offset_aligned, SEEK_SET);
    // 按一个逻辑块读写
    while (size_aligned != 0)
    {
        ddriver_read(super.fd, cur, NEWFS_IO_SIZE());
        cur          += NEWFS_IO_SIZE();
        size_aligned -= NEWFS_IO_SIZE();   
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return NEWFS_ERROR_NONE;
}

/**
 * @brief 驱动写
 * 
 * @param offset 
 * @param in_content 
 * @param size 
 * @return int 
 */
int newfs_driver_write(int offset, uint8_t *in_content, int size) {
    // 按照一个逻辑块大小(1024B)封装
    int      offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_BLK_SIZE());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NEWFS_ROUND_UP((size + bias), NEWFS_BLK_SIZE());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    // 读出需要的磁盘块到内存
    newfs_driver_read(offset_aligned, temp_content, size_aligned);
    // 在内存覆盖指定内容
    memcpy(temp_content + bias, in_content, size);
    
    ddriver_seek(super.fd, offset_aligned, SEEK_SET);

    // 将读出的磁盘块再依次写回到内存
    while (size_aligned != 0)
    {
        ddriver_write(super.fd, cur, NEWFS_IO_SIZE());
        cur          +=  NEWFS_IO_SIZE();
        size_aligned -=  NEWFS_IO_SIZE();   
    }

    free(temp_content);
    return NEWFS_ERROR_NONE;
}

/**
 * @brief 将denry插入到inode中，采用头插法
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int newfs_alloc_dentry(struct newfs_inode* inode, struct newfs_dentry* dentry) {
    if (inode->dentrys == NULL) {
        inode->dentrys = dentry;
    }
    else {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;
    int current_blk = inode->dir_cnt / MAX_DENTRY_PERBLK();
    //当前数据块满，寻找新数据块
    if(inode->dir_cnt % MAX_DENTRY_PERBLK() == 1){
        newfs_alloc_data(inode, current_blk);
    }
    return inode->dir_cnt;
}

/**
 * @brief 将dentry从inode的dentrys中取出
 * 
 * @param inode 一个目录的索引结点
 * @param dentry 该目录下的一个目录项
 * @return int 
 */
int newfs_drop_dentry(struct newfs_inode * inode, struct newfs_dentry * dentry) {
    boolean is_find = FALSE;
    struct newfs_dentry* dentry_cursor;
    dentry_cursor = inode->dentrys;
    
    if (dentry_cursor == dentry) {
        inode->dentrys = dentry->brother;
        is_find = TRUE;
    }
    else {
        while (dentry_cursor)
        {
            if (dentry_cursor->brother == dentry) {
                dentry_cursor->brother = dentry->brother;
                is_find = TRUE;
                break;
            }
            dentry_cursor = dentry_cursor->brother;
        }
    }
    if (!is_find) {
        return -NEWFS_ERROR_NOTFOUND;
    }
    inode->dir_cnt--;
    free(dentry);
    return inode->dir_cnt;
}

/**
 * @brief 分配一个inode，占用位图
 * 
 * @param dentry 该dentry指向分配的inode
 * @return newfs_inode
 */
struct newfs_inode* newfs_alloc_inode(struct newfs_dentry * dentry) {
    struct newfs_inode* inode;
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int dno_cursor  = 0;
    boolean is_find_free_blk = FALSE;
    /* 检查位图是否有空位 */
    for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SIZE(super.ino_map_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前dno_cursor位置空闲 */
                super.map_inode[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_blk = TRUE;           
                break;
            }
            dno_cursor++;
        }
        if (is_find_free_blk) {
            break;
        }
    }

    if (!is_find_free_blk || dno_cursor == super.ino_max)
        return -NEWFS_ERROR_NOSPACE;

    inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    inode->ino  = dno_cursor; 
    inode->size = 0;
    /* dentry指向inode */
    dentry->inode = inode;
    dentry->ino   = inode->ino;
    /* inode指回dentry */
    inode->dentry = dentry; //指向该inode的目录项
    
    inode->dir_cnt = 0;
    inode->dentrys = NULL;
    
    if (NEWFS_IS_REG(inode)) {
        for(int i = 0;i<MAX_DATA_PERFILE;i++)
            inode->data[i] = (uint8_t *)malloc(NEWFS_BLK_SIZE());
    }

    for(int i = 0;i<MAX_DATA_PERFILE;i++)
        inode->block_pointer[i] = -1;

    return inode;
}

/**
 * @brief 删除内存中的一个inode
 * @param inode 
 * @return int 
 */
int newfs_drop_inode(struct newfs_inode * inode) {
    struct newfs_dentry*  dentry_cursor;
    struct newfs_dentry*  dentry_to_free;
    struct newfs_inode*   inode_cursor;

    if (inode == super.root_dentry->inode) {
        return NEWFS_ERROR_INVAL;
    }

    if (NEWFS_IS_DIR(inode)) {
        dentry_cursor = inode->dentrys;

        /* 递归向下drop */
        while (dentry_cursor)
        {   
            inode_cursor = dentry_cursor->inode;
            newfs_drop_inode(inode_cursor);
            newfs_drop_dentry(inode, dentry_cursor);
            dentry_to_free = dentry_cursor;
            dentry_cursor = dentry_cursor->brother;
            free(dentry_to_free);
        }
    }
    else if (NEWFS_IS_REG(inode)) {
        /* 调整datamap */
        for(int blk_no = 0; blk_no < MAX_DATA_PERFILE; blk_no++) {
            free(inode->data[blk_no]);
            if(inode->block_pointer[blk_no] == -1) continue;
            boolean find = FALSE;
            for(int i = 0;i < NEWFS_BLKS_SIZE(super.db_map_blks);i++) {
                for(int j = 0;j < UINT8_BITS; j++) {
                    if(inode->block_pointer[blk_no] = i * 8 + j) {
                        super.map_db[i] &= (uint8_t)(~(0x1 << j));
                        find = TRUE;
                        break;
                    }
                }
                if(find) break;
            }
        }
    }

     /* 调整inodemap */
    boolean find = FALSE;
    for(int i = 0;i < NEWFS_BLKS_SIZE(super.ino_map_blks);i++) {
        for(int j = 0;j < UINT8_BITS; j++) {
            if(inode->ino = i * 8 + j) {
                super.map_inode[i] &= (uint8_t)(~(0x1 << j));
                find = TRUE;
                break;
            }
        }
        if(find) break;
    }

    free(inode);
    return NEWFS_ERROR_NONE;
}

/**
 * @brief 分配一个data block，占用位图
 * 
 * @param inode 需要分配数据块的文件inode
 * @param blk_no 该文件的第几块
 * @return int
 */
int newfs_alloc_data(struct newfs_inode* inode,int blk_no) {
    boolean find = FALSE;
    /* 检查位图是否有空位 */
    for(int i = 0;i < super.ino_max / UINT8_BITS;i++) {
            for(int j =0;j < UINT8_BITS; j++) {
                if(!(super.map_db[i] & (0x1 << j))) {   //第8*j+i位为0，分配
                    super.map_db[i] |= (0x1 << j);      //置1
                    inode->block_pointer[blk_no] = 8 * j + i;
                    find = TRUE;
                    break;
                }
            }
            if(find) break;
        }

    if (!find) return -NEWFS_ERROR_NOSPACE;
    
    return NEWFS_ERROR_NONE;
}

/**
 * @brief 
 * 
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct newfs_inode* 
 */
struct newfs_inode* newfs_read_inode(struct newfs_dentry * dentry, int ino) {
    struct newfs_inode* inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    struct newfs_inode_d inode_d;
    struct newfs_dentry* sub_dentry;
    struct newfs_dentry_d dentry_d;
    int    dir_cnt = 0, i;
    /* 从磁盘读索引结点 */
    if (newfs_driver_read(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                        sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE) {
        NEWFS_DBG("[%s] io error\n", __func__);
        return NULL;                    
    }
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    inode->dentry = dentry;
    inode->dentrys = NULL;
    for(int i = 0; i < MAX_DATA_PERFILE; i++){
        inode->block_pointer[i] = inode_d.block_pointer[i];
    }

    //判断结点类型
    // 节点是目录，读取每一个目录项
    if (NEWFS_IS_DIR(inode)) {
        dir_cnt = inode_d.dir_cnt;
        // 对于节点指向的所有数据块
        for(i = 0;i < MAX_DATA_PERFILE;i++) {
            if(!dir_cnt) break; //没有目录项了   
            int offset = NEWFS_DB_OFS(inode->block_pointer[i]);
            // 对于每个数据块，逐dentry读取
            while(dir_cnt && offset + sizeof(struct newfs_dentry_d) < NEWFS_DB_OFS(inode->block_pointer[i] + 1)) {
                if (newfs_driver_read(offset, (uint8_t *)&dentry_d, 
                    sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE) {
                NEWFS_DBG("[%s] io error\n", __func__);
                return NULL;
                }
                sub_dentry = new_dentry(dentry_d.name, dentry_d.ftype);
                sub_dentry->parent = inode->dentry;
                sub_dentry->ino    = dentry_d.ino; 
                newfs_alloc_dentry(inode, sub_dentry);

                //下一个dentry
                offset += sizeof(struct newfs_dentry_d);
                dir_cnt--;
            }
        }
    }

    //节点是文件
    else if (NEWFS_IS_REG(inode)) {
        for(i = 0;i < MAX_DATA_PERFILE; i++) {
            inode->data[i] = (uint8_t *)malloc(NEWFS_BLK_SIZE());
            if (newfs_driver_read(NEWFS_DB_OFS(inode->block_pointer[i]), (uint8_t *)inode->data[i], 
                NEWFS_BLK_SIZE()) != NEWFS_ERROR_NONE) {
                NEWFS_DBG("[%s] io error\n", __func__);
                return NULL;                    
            }
        }
    }
    return inode;
}

/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 * 
 * @param inode 
 * @return int 
 */
int newfs_sync_inode(struct newfs_inode *inode) {
    struct newfs_inode_d  inode_d;
    struct newfs_dentry*  dentry_cursor;
    struct newfs_dentry*  pre_dentry_cursor;
    struct newfs_dentry_d dentry_d;
    int ino             = inode->ino;
    
    //将内存的inode刷回磁盘的inode_d
    inode_d.ino         = ino;
    inode_d.size        = inode->size;
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;

    for(int i = 0;i < MAX_DATA_PERFILE; i++) 
        inode_d.block_pointer[i] = inode->block_pointer[i];

    /* 先写inode本身 */
    if (newfs_driver_write(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                     sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE) {
        NEWFS_DBG("[%s] io error\n", __func__);
        return -NEWFS_ERROR_IO;
    }

    /* 再写inode下方的数据 */
    if (NEWFS_IS_DIR(inode)) { /* 如果当前inode是目录，那么数据是目录项，且目录项的inode也要写回 */                          
        dentry_cursor = inode->dentrys;

        for(int i = 0;i < MAX_DATA_PERFILE;i++) {
            if(dentry_cursor == NULL) break;
            int offset = NEWFS_DB_OFS(inode->block_pointer[i]);
            while ((dentry_cursor != NULL) && (offset < NEWFS_DB_OFS(inode->block_pointer[i] + 1))) {
                memcpy(dentry_d.name, dentry_cursor->name, MAX_NAME_LEN);
                dentry_d.ftype = dentry_cursor->ftype;
                dentry_d.ino = dentry_cursor->ino;
                if (newfs_driver_write(offset, (uint8_t *)&dentry_d, 
                    sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE) {
                    NEWFS_DBG("[%s] io error\n", __func__);
                    return -NEWFS_ERROR_IO;                     
                }
                
                //递归调用
                if (dentry_cursor->inode != NULL) {
                    newfs_sync_inode(dentry_cursor->inode);
                }

                //下一个目录项
                pre_dentry_cursor = dentry_cursor;
                dentry_cursor = dentry_cursor->brother;
                free(pre_dentry_cursor);
                
                offset += sizeof(struct newfs_dentry_d);
            }
        }
    }
    else if (NEWFS_IS_REG(inode)) { /* 如果当前inode是文件，那么数据是文件内容，直接写即可 */
        for(int i =0;i < MAX_DATA_PERFILE;i++) {
            if(inode->block_pointer[i] == -1) continue;
            if (newfs_driver_write(NEWFS_DB_OFS(inode->block_pointer[i]), inode->data[i], 
                NEWFS_BLK_SIZE()) != NEWFS_ERROR_NONE) {
                NEWFS_DBG("[%s] io error\n", __func__);
                return -NEWFS_ERROR_IO;
            }
            free(inode->data[i]);
        }
    }
    free(inode);
    return NEWFS_ERROR_NONE;
}

/**
 * @brief 
 * 
 * @param inode 
 * @param dir [0...]
 * @return struct newfs_dentry* 
 */
struct newfs_dentry* newfs_get_dentry(struct newfs_inode * inode, int dir) {
    struct newfs_dentry* dentry_cursor = inode->dentrys;
    int    cnt = 0;
    while (dentry_cursor)
    {
        if (dir == cnt) {
            return dentry_cursor;
        }
        cnt++;
        dentry_cursor = dentry_cursor->brother;
    }
    return NULL;
}

/**
 * @brief 查找文件或目录
 * path: /qwe/ad  total_lvl = 2,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry 
 *      3) find qwe's inode     lvl = 2
 *      4) find ad's dentry
 *
 * path: /qwe     total_lvl = 1,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 *  
 * 
 * 如果能查找到，返回该目录项
 * 如果查找不到，返回的是上一个有效的路径
 * 
 * path: /a/b/c
 *      1) find /'s inode     lvl = 1
 *      2) find a's dentry 
 *      3) find a's inode     lvl = 2
 *      4) find b's dentry    如果此时找不到了，is_find=FALSE且返回的是a的inode对应的dentry
 * 
 * @param path 
 * @return struct newfs_dentry* 
 */
struct newfs_dentry* newfs_lookup(const char * path, boolean* is_find, boolean* is_root) {
    struct newfs_dentry* dentry_cursor = super.root_dentry;
    struct newfs_dentry* dentry_ret = NULL;
    struct newfs_inode*  inode; 
    int   total_lvl = newfs_calc_lvl(path);
    int   lvl = 0;
    boolean is_hit;
    char* fname = NULL;
    char* path_cpy = (char*)malloc(sizeof(path));
    *is_root = FALSE;
    strcpy(path_cpy, path);

    if (total_lvl == 0) {                           /* 根目录 */
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = super.root_dentry;
    }

    //获取第一层文件夹名
    fname = strtok(path_cpy, "/");       
    while (fname)
    {   
        lvl++;
        if (dentry_cursor->inode == NULL) {           /* Cache机制 */
            newfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        // 获取当前inode对应的inode
        inode = dentry_cursor->inode;

        //文件夹名是文件类型，路径出错
        if (NEWFS_IS_REG(inode) && lvl < total_lvl) {
            NEWFS_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }

        //是文件夹类型，遍历目录项
        if (NEWFS_IS_DIR(inode)) {
            dentry_cursor = inode->dentrys; //指向头dentry
            is_hit        = FALSE;

            while (dentry_cursor)   /* 遍历子目录项 */
            {
                if (memcmp(dentry_cursor->name, fname, strlen(fname)) == 0) {
                    is_hit = TRUE;
                    break;
                }
                dentry_cursor = dentry_cursor->brother;
            }
            
            //没在该层文件夹找到该文件，报错，退出
            if (!is_hit) {
                *is_find = FALSE;
                NEWFS_DBG("[%s] not found %s\n", __func__, fname);
                dentry_ret = inode->dentry;
                break;
            }

            //查到正确的文件
            if (is_hit && lvl == total_lvl) {
                *is_find = TRUE;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        fname = strtok(NULL, "/"); 
    }

    //从磁盘中读出目标inode
    if (dentry_ret->inode == NULL) {
        dentry_ret->inode = newfs_read_inode(dentry_ret, dentry_ret->ino);
    }
    
    return dentry_ret;
}

/**
 * @brief 挂载newfs, Layout 如下
 * 
 * Layout
 * | Super | Inode Map | Data Map | Inode | Data |
 * 
 * BLK_SZ = IO_SZ * 2
 * 
 * @param options 
 * @return int 
 */
int newfs_mount(struct custom_options options){
    int ret = NEWFS_ERROR_NONE;
    int driver_fd;
    struct newfs_super_d  newfs_super_d; 
    struct newfs_dentry*  root_dentry;
    struct newfs_inode*   root_inode;
    boolean             is_init = FALSE;

    super.is_mounted = FALSE;

    driver_fd = ddriver_open(newfs_options.device);
    if (driver_fd < 0) return driver_fd;
    super.fd = driver_fd;

    ddriver_ioctl(super.fd, IOC_REQ_DEVICE_SIZE,  &super.disk_size);
    ddriver_ioctl(super.fd, IOC_REQ_DEVICE_IO_SZ,  &super.io_size);
    super.blks_size = NEWFS_IO_SIZE() * 2;
    super.blks_nums = super.disk_size / NEWFS_BLK_SIZE();

    root_dentry = new_dentry("/", DIR);

    if(newfs_driver_read(NEWFS_SUPER_OFS, (uint8_t *)(&newfs_super_d),
        sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE) {
                return -NEWFS_ERROR_IO;
    }

    if(newfs_super_d.magic != NEWFS_MAGIC) {
        newfs_super_d.sb_blks           = SUPER_BLKS_NUM;
        newfs_super_d.sb_offset         = NEWFS_SUPER_OFS;
        newfs_super_d.ino_map_blks      = INODE_MAP_BLKS_NUM;
        newfs_super_d.ino_map_offset    = newfs_super_d.sb_offset + NEWFS_BLKS_SIZE(newfs_super_d.sb_blks);
        newfs_super_d.db_map_blks       = DATA_MAP_BLKS_NUM;
        newfs_super_d.db_map_offset     = newfs_super_d.ino_map_offset + NEWFS_BLKS_SIZE(newfs_super_d.ino_map_blks);
        newfs_super_d.ino_blks          = MAX_INODE_BLKS_NUM;
        newfs_super_d.ino_offset        = newfs_super_d.db_map_offset + NEWFS_BLKS_SIZE(newfs_super_d.db_map_blks);
        newfs_super_d.db_blks           = MAX_DATA_BLKS_NUM;
        newfs_super_d.db_offset         = newfs_super_d.ino_offset + NEWFS_BLKS_SIZE(newfs_super_d.ino_blks);

        newfs_super_d.ino_max           = MAX_INODE_BLKS_NUM * MAX_INODE_NUM_PERBLK;
        newfs_super_d.file_max          = MAX_DATA_BLKS_NUM;

        is_init = TRUE;
    }

    /* 建立in mem结构 */
    super.sb_blks                   = newfs_super_d.sb_blks;
    super.sb_offset                 = newfs_super_d.sb_offset;
    super.ino_map_blks              = newfs_super_d.ino_map_blks;
    super.ino_map_offset            = newfs_super_d.ino_map_offset; 
    super.db_map_blks               = newfs_super_d.db_map_blks; 
    super.db_map_offset             = newfs_super_d.db_map_offset; 
    super.ino_blks                  = newfs_super_d.ino_blks;
    super.ino_offset                = newfs_super_d.ino_offset; 
    super.db_blks                   = newfs_super_d.db_blks; 
    super.db_offset                 = newfs_super_d.db_offset; 

    super.ino_max                   = newfs_super_d.ino_max; 
    super.file_max                  = newfs_super_d.file_max; 

    super.map_inode = (uint8_t *)malloc(NEWFS_BLKS_SIZE(newfs_super_d.ino_map_blks));
    super.map_db = (uint8_t *)malloc(NEWFS_BLKS_SIZE(newfs_super_d.db_map_blks));
    // newfs_dump_map();

    if(newfs_driver_read(super.ino_map_offset, (uint8_t *)(super.map_inode), 
        NEWFS_BLKS_SIZE(super.ino_map_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    if(newfs_driver_read(super.db_map_offset, (uint8_t *)(super.map_db), 
        NEWFS_BLKS_SIZE(super.db_map_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }


    /* 初始化根目录 */
    if(is_init) {
        root_inode = newfs_alloc_inode(root_dentry);
        newfs_sync_inode(root_inode);
    }

    root_inode = newfs_read_inode(root_dentry, NEWFS_ROOT_INO);
    root_dentry->inode = root_inode;
    super.root_dentry = root_dentry;
    super.is_mounted = TRUE;

    newfs_dump_map();
    return ret;
}

/**
 * @brief 卸载newfs
 * 
 * @return int 
 */
int newfs_umount(){
struct newfs_super_d  newfs_super_d; 

    if (!super.is_mounted) {
        return NEWFS_ERROR_NONE;
    }

    newfs_sync_inode(super.root_dentry->inode);     /* 从根节点向下刷写节点 */
                                                    
    newfs_super_d.magic             = NEWFS_MAGIC_NUM;
    newfs_super_d.sb_blks           = super.sb_blks;
    newfs_super_d.sb_offset         = super.sb_offset;
    newfs_super_d.ino_map_blks      = super.ino_map_blks;
    newfs_super_d.ino_map_offset    = super.ino_map_offset;
    newfs_super_d.db_map_blks       = super.db_map_blks;
    newfs_super_d.db_map_offset     = super.db_map_offset;
    newfs_super_d.ino_blks          = super.ino_blks;
    newfs_super_d.ino_offset        = super.ino_offset;
    newfs_super_d.db_blks           = super.db_blks;
    newfs_super_d.db_offset         = super.db_offset;

    newfs_super_d.ino_max           = super.ino_max;
    newfs_super_d.file_max          = super.file_max;

    //刷超级块进磁盘
    if (newfs_driver_write(NEWFS_SUPER_OFS, (uint8_t *)&newfs_super_d, 
                     sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    //刷节点位图进磁盘
    if (newfs_driver_write(newfs_super_d.ino_map_offset, (uint8_t *)(super.map_inode), 
                         NEWFS_BLKS_SIZE(newfs_super_d.ino_map_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    //刷数据位图进磁盘
    if (newfs_driver_write(newfs_super_d.db_map_offset, (uint8_t *)(super.map_db), 
                         NEWFS_BLKS_SIZE(newfs_super_d.db_map_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    free(super.map_inode);
    free(super.map_db);

    //关闭驱动
    ddriver_close(super.fd);

    return NEWFS_ERROR_NONE;
}