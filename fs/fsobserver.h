#ifndef _SDCARD_OBSERVER_H
#define _SDCARD_OBSERVER_H

#include <linux/errno.h>

#ifdef __KERNEL__

#define ENABLE_FILE_OBSERVER

#ifdef ENABLE_FILE_OBSERVER


#define FSOBSERVER_FS_MODIFY    0x0001  /* File was modified */
#define FSOBSERVER_FS_FIRST_READ    0x0002  /* File was readed */

#define FSOBSERVER_FS_MASK    0x0000000000000003  /*mask */


#define FSOBSERVER_FILEOBSERVER_IOCTL_CMD 1000
#define USER_NETLINK_CMD    26
#define MAXMSGLEN           1024


#define INODE_INUM_LEN_MAX 64
#define PROCESS_NAME_MAX 128
#define NL_MSG_MAX (PATH_MAX+PROCESS_NAME_MAX+128)
#define NL_RNAME_MSG_MAX (PATH_MAX*2+PROCESS_NAME_MAX+128)

typedef enum type_e {
    NL_TYPE_NONE,
    NL_TYPE_SET_DEST_ADDR,
    NL_TYPE_SET_ENABLE,
    NL_TYPE_SET_DISABLE,
    NL_TYPE_SET_OPTION,
    NL_TYPE_ENABLE_READ,
    NL_TYPE_DISABLE_READ
} netlink_type;


typedef enum FO_OP_e {
    OP_CREATE,
    OP_RENAME,
    OP_CLOSE_WRITE,
    OP_UNLINK,
    OP_MKDIR,
    OP_RMDIR,
    OP_FIRST_READ,
} FO_OP;

typedef enum FO_FRAGEMENT_e {
    FRAGEMENT_INO,
    FRAGEMENT_SIZE,
    FRAGEMENT_UID,
    FRAGEMENT_PID,
    FRAGEMENT_CUID,
    FRAGEMENT_CPID,
    FRAGEMENT_TNAME,
    FRAGEMENT_PNAME,
    FRAGEMENT_PATH,
    FRAGEMENT_OLDPATH,
    FRAGEMENT_NEWPATH,
} FO_FRAGEMENT;

typedef struct FO_FragMent_t {
    int fm;
    int offset;
    int len;
}FO_FragMent;

typedef struct FO_Header_t {
    char flag[4];
    int op;
    int fmCnt;
}FO_Header;


typedef struct Last_Msg_t {
    unsigned long ino;
    long time;
    unsigned char data[NL_MSG_MAX];
    int volatile data_len;
    bool volatile is_valid;
}Last_Msg;

struct fsobserver_file_creator {
    uid_t uid;
    pid_t pid;
};



int fsobserver_init(void);
int fsobserver_exit(void);
bool nubia_permission(void);
int fsobserver_post_release(struct inode *inode, struct file *file);
//int fsobserver_post_open(struct file *file);
int fsobserver_post_read(struct file *file);
int fsobserver_post_write(struct file *file) ;
int fsobserver_post_create(struct dentry *dentry) ;
int fsobserver_post_unlink(struct inode *dir, struct dentry *dentry) ;
int fsobserver_post_mkdir(struct inode *dir, struct dentry *dentry) ;
int fsobserver_post_rmdir(struct inode *dir, struct dentry *dentry) ;
int fsobserver_post_rename(struct inode *old_dir, struct dentry *old_dentry,
             struct inode *new_dir, struct dentry *new_dentry) ;
bool fsobserver_ioctl(struct file *file, unsigned int cmd, unsigned long arg) ;
#endif

#endif /* __KERNEL__ */
#endif /* _LINUX_MM_H */
