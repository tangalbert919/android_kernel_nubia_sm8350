/*
 * fs/fsobserver.c
 *
 * Copyright (c) 2017 Nubia Technology Co. Ltd
 *   Author: wuzhibin
 *
 * This file is dual licensed.  It may be redistributed and/or modified
 * under the terms of the Apache 2.0 License OR version 2 of the GNU
 * General Public License.
 */

#include <linux/types.h>
#include <linux/parser.h>

#include <linux/netlink.h>
#include <net/sock.h>
#include "fsobserver.h"
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/mount.h>
#include <mount.h>
#include <linux/mtd/mtd.h>
#include "linux/jiffies.h"
#include "linux/dcache.h"

#ifdef ENABLE_FILE_OBSERVER

#define WATCH_FLAG_MOUNTPOINT_EMULATED  (1 << 0)
#define WATCH_FLAG_MOUNTPOINT_DATA      (1 << 1)
#define WATCH_FLAG_MOUNTPOINT_SYS       (1 << 2)
#define WATCH_FLAG_MOUNTPOINT_DEV       (1 << 3)
#define WATCH_FLAG_MOUNTPOINT_PERSIST   (1 << 4)
#define WATCH_FLAG_MOUNTPOINT_CPUSET    (1 << 5)


#define WATCH_FLAG_FSTYPE_F2FS        (1 << 8)
#define WATCH_FLAG_FSTYPE_SDCARDFS    (1 << 9)
#define WATCH_FLAG_FSTYPE_SYSFS       (1 << 10)
#define WATCH_FLAG_FSTYPE_CGROUP      (1 << 11)
#define WATCH_FLAG_FSTYPE_FUSE        (1 << 12)

#define WATCH_FLAG_READ               (1 << 13)





#define WATCH_MOUNTPOINT_EMULATED  "emulated"
#define WATCH_MOUNTPOINT_DATA      "data"
#define WATCH_MOUNTPOINT_SYS       "sys"
#define WATCH_MOUNTPOINT_ACCT      "acct"
#define WATCH_MOUNTPOINT_BLKIO     "blkio"
#define WATCH_MOUNTPOINT_STUNE     "stune"
#define WATCH_MOUNTPOINT_PERSIST   "persist"
#define WATCH_MOUNTPOINT_CPUSET    "cpuset"
#define WATCH_MOUNTPOINT_USER_DE   "user_de"
#define WATCH_MOUNTPOINT_CUR       "cur"
#define WATCH_MOUNTPOINT_DEV       "dev"
#define WATCH_MOUNTPOINT_USER_CE   "user_ce"
#define WATCH_MOUNTPOINT_ROOT      "/"


#define WATCH_FSTYPE_F2FS           "f2fs"
#define WATCH_FSTYPE_SELINUXFS      "selinuxfs"
#define WATCH_FSTYPE_CGROUP         "cgroup"
#define WATCH_FSTYPE_PROC           "proc"
#define WATCH_FSTYPE_TRACEFS        "tracefs"
#define WATCH_FSTYPE_SYSFS          "sysfs"
#define WATCH_FSTYPE_SDCARDFS       "sdcardfs"
#define WATCH_FSTYPE_FUSE           "fuse"
#define WATCH_FSTYPE_CONFIGFS       "configfs"
#define WATCH_FSTYPE_TEMPFS         "tempfs"
#define WATCH_FSTYPE_OVERLAYFS      "overlay"
#define WATCH_FSTYPE_EXT4FS         "ext4"



#define CMD_LINE_MAX 1024


static struct sock *netlink_fd = NULL;
static int g_pid = -1;
static bool gEnable = true;
static bool gEnableRead = false;
static unsigned short gflags = 0;

#define FILE_PATH_MAX 4096

static Last_Msg g_last_msg;

#define DEBUG 0
#define DEBUG_DUMP_STACK 0


extern unsigned long volatile jiffies;

#define EXTERNAL_STORAGE_PREFIX "/storage"

static bool isWatched(struct dentry *dentry, struct inode *inode, struct file *file) {
    unsigned short  fstype = 0;
    unsigned char mnt_point_flag  = 0;
    struct super_block *sb = NULL;
    struct mount *mnt;

    if(sb == NULL && dentry != NULL) {
        sb = dentry->d_sb;
    }
    if(sb == NULL && inode != NULL) {
        sb = inode->i_sb;
    }
    if(sb == NULL  && file != NULL) {
        if(file->f_path.dentry != NULL) {
            sb = file->f_path.dentry->d_sb;
        }
        if(sb == NULL && file->f_inode != NULL) {
            sb = file->f_inode->i_sb;
        }
    }
    if(sb == NULL) {
        return false;
    }
    if(sb->s_type != NULL) {
        //printk("sblk_fstype:%s s_subtype:%s\n", sb->s_type->name, sb->s_subtype);
        if(sb->s_type->name != NULL &&  (strcmp(sb->s_type->name, WATCH_FSTYPE_F2FS) == 0)) {
            fstype |= WATCH_FLAG_FSTYPE_F2FS;
        } else if(sb->s_type->name != NULL &&  (strcmp(sb->s_type->name, WATCH_FSTYPE_SDCARDFS) == 0)) {
            fstype |= WATCH_FLAG_FSTYPE_SDCARDFS;
        } else if(sb->s_type->name != NULL &&  (strcmp(sb->s_type->name, WATCH_FSTYPE_FUSE) == 0)) {
            fstype |= WATCH_FLAG_FSTYPE_FUSE;
        } /* else if(sb->s_type->name != NULL &&  (strcmp(sb->s_type->name, WATCH_FSTYPE_SYSFS) == 0)) {
            fstype |= WATCH_FLAG_FSTYPE_SYSFS;
        } else if(sb->s_type->name != NULL &&  (strcmp(sb->s_type->name, WATCH_FSTYPE_CGROUP) == 0)) {
            fstype |= WATCH_FLAG_FSTYPE_CGROUP;
        }*/
    }
    if(fstype == 0) {
        return false;
    }
    #if DEBUG
    //printk("gflags:%u fstype:%u\n", gflags, fstype);
    #endif
    if(((gflags & 0xFF00) & fstype) > 0) {//file system type match
        #if DEBUG
        if(sb->s_type != NULL) {
            //printk("sblk_fstype:%s s_subtype:%s\n", sb->s_type->name, sb->s_subtype);
        }
        #endif

        mnt = list_first_entry(&sb->s_mounts, typeof(*mnt), mnt_instance);
        if(mnt != NULL && mnt->mnt_mountpoint != NULL) {
            #if DEBUG
             //printk("mount_point:%s\n", mnt->mnt_mountpoint->d_name.name);
            #endif
            if(mnt->mnt_mountpoint->d_name.name != NULL) {
                 if(strcmp(mnt->mnt_mountpoint->d_name.name, WATCH_MOUNTPOINT_EMULATED) == 0) {
                    mnt_point_flag |= WATCH_FLAG_MOUNTPOINT_EMULATED;
                 } else if(strcmp(mnt->mnt_mountpoint->d_name.name, WATCH_MOUNTPOINT_DATA) == 0) {
                    mnt_point_flag |= WATCH_FLAG_MOUNTPOINT_DATA;
                 }
                 //other mount point...
             }
        }
        #if DEBUG
        printk("gflags:%u mnt_point_flag:%u\n", gflags, mnt_point_flag);
        #endif

        if( ((gflags & 0x00FF) & mnt_point_flag) > 0 ) {//mount point match
            return true;
        }
    }

    return false;//default not watched
}

static bool isAllow(struct dentry *dentry, struct inode *inode, struct file *file) {
    if(!gEnable)
        return false;

    if(g_pid == -1)
        return false;

    return isWatched(dentry, inode, file);
}

bool nubia_permission(void) {
    if(!gEnable)
        return false;

    if(!gEnableRead) {
        return false;
    }

    if(current->cred->uid.val == 0 && strncmp(current->comm, "fileobserverd", 13) == 0) {
        return true;
    }
    return false;
}

static void fsobserver_netlink_process_packet(struct nlmsghdr *nl){
    switch(nl->nlmsg_type)  {
            case NL_TYPE_SET_DEST_ADDR:
                g_pid = nl->nlmsg_pid;
                gflags = nl->nlmsg_flags;
                if( (gflags & WATCH_FLAG_READ) > 0) {
                    gEnableRead = true;
                }
                printk(KERN_DEBUG"[fsobserver] got the FileObserverD pid %d flags:%x read:%d\r\n", nl->nlmsg_pid, gflags, gEnableRead);
                break;
            case NL_TYPE_SET_ENABLE:
                gEnable = true;
                g_pid = nl->nlmsg_pid;
                gflags = nl->nlmsg_flags;
                if( (gflags & WATCH_FLAG_READ) > 0) {
                    gEnableRead = true;
                }
                printk(KERN_DEBUG"[fsobserver] Enable file observer pid: %d flags:%x read:%d\r\n", nl->nlmsg_pid, gflags, gEnableRead);

                break;
            case NL_TYPE_SET_DISABLE:
                printk(KERN_DEBUG"[fsobserver] Disable file observer\r\n");
                gEnable = false;
                break;
            case NL_TYPE_ENABLE_READ:
                gEnableRead = true;
                printk(KERN_DEBUG"[fsobserver] enable watch read\r\n");
                break;
            case NL_TYPE_DISABLE_READ:
                gEnableRead = false;
                printk(KERN_DEBUG"[fsobserver] disable watch read\r\n");
                break;
            default:break;
    }
}
static void fsobserver_netlink_recv_packet(struct sk_buff *__skb){
    struct sk_buff *skb;
    struct nlmsghdr *nlhdr;
    skb = skb_get(__skb);
    printk(KERN_DEBUG"[fsobserver] recv packet, %u\r\n", skb->len);
    if(skb->len >= sizeof(struct nlmsghdr)) {
        nlhdr = (struct nlmsghdr *)skb->data;
        printk(KERN_DEBUG"[fsobserver] recv packet, %u %u\r\n", nlhdr->nlmsg_len, __skb->len);
        if(nlhdr->nlmsg_len >= sizeof(struct nlmsghdr) &&    __skb->len >= nlhdr->nlmsg_len)    {
            fsobserver_netlink_process_packet(nlhdr);
        } else {
            printk(KERN_WARNING "[fsobserver] msg len error!\n");
        }
    } else {
        printk(KERN_WARNING "[fsobserver] Kernel receive msg length error!\n");
    }
}
static struct netlink_kernel_cfg fo_netlink_cfg = {
        .input = fsobserver_netlink_recv_packet
};

int fsobserver_init(void) {
    if(!gEnable)
        return 0;
    printk(KERN_WARNING "[fsobserver] fsobserver_init\n");
    if(netlink_fd != NULL) {
        printk(KERN_WARNING "[fsobserver] fsobserver netlink already init!\n");
        return 0;
    }

    netlink_fd = netlink_kernel_create(&init_net, USER_NETLINK_CMD, &fo_netlink_cfg);
    if(NULL == netlink_fd)  {
        printk(KERN_WARNING "[fsobserver] init fsobserver netlink fail!\n");
        return -1;
    }
    g_last_msg.data_len = 0;
    g_last_msg.ino = 0;
    g_last_msg.time = 0;
    g_last_msg.is_valid = false;

    printk(KERN_WARNING "[fsobserver] fsobserver moudle inited.\n");
    #if DEBUG_DUMP_STACK
    dump_stack();
    #endif
    return 0;
}
int fsobserver_exit(void) {
    if(!gEnable)
        return 0;

    if(NULL != netlink_fd)  {
        netlink_kernel_release(netlink_fd);
    }
    netlink_fd = NULL;
    printk(KERN_INFO, "[fsobserver] fsobserver moudle exited.\n");
    return 0;
}

static int fsobserver_nl_send_msg(const u8 *data, int len)    {
    struct nlmsghdr *rep;
    u8 *res;
    struct sk_buff *skb;

    if(g_pid == -1)
        return -1;
    if(data == NULL)
        return -1;
    if(len <=0)
        return -1;
    if(netlink_fd == NULL) {
        printk("Error, fsobserver netlink not init!\n");
        return -1;
    }

    skb = nlmsg_new(len, GFP_KERNEL);
    if(!skb) {
        printk("nlmsg_new failed!\n");
        return -1;
    }
    #if DEBUG_DUMP_STACK
    dump_stack();
    #endif

    #if DEBUG
    printk("Send Msg To User len:%d\n", len);
    #endif
    rep = nlmsg_put(skb, g_pid, 0, NLMSG_NOOP, len, 0);
    res = nlmsg_data(rep);
    memcpy(res, data, len);
    netlink_unicast(netlink_fd, skb, g_pid, MSG_DONTWAIT);
    return 0;
}

#if DEBUG
static const char* fsobserver_print_info(char* tip, struct dentry *dentry) {
    struct super_block *sb = dentry->d_sb;
    struct mount *mnt;
    struct vfsmount *vfsmnt;
    mnt = list_first_entry(&sb->s_mounts, typeof(*mnt), mnt_instance);
    if(mnt != NULL && mnt->mnt_mountpoint != NULL) {
        vfsmnt = &(mnt->mnt);
        if(vfsmnt != NULL) {
            printk("%s mountpoint:%s dentry:%s mnt_root:%s fstype:%s s_subtype:%s\n", tip, mnt->mnt_mountpoint->d_name.name, dentry->d_name.name, vfsmnt->mnt_root->d_name.name, sb->s_type->name, sb->s_subtype);
        }
    }
    return NULL;

}
#endif

#if 1
static ssize_t get_node_path_locked(struct dentry *dentry, char* buf, size_t bufsize) {
    const char* name;
    size_t namelen;
    ssize_t pathlen = 0;
    if(dentry == NULL) {
        return -1;
    }
    //printk("get_node_path_locked 22222 dentry->d_name.name: %s   d_iname:%s\r\n", dentry->d_name.name, dentry->d_iname);
    name = dentry->d_name.name;//d_iname;//
    namelen = strlen(name);
    if (bufsize < namelen + 1) {
        return -1;
    }
    //printk("--->%s\n", dentry->d_iname);
    if(dentry == dentry->d_parent) {
        #if DEBUG
        fsobserver_print_info("reach head ", dentry);
        #endif
        return 0;
    } else {
        pathlen = get_node_path_locked(dentry->d_parent, buf, bufsize - namelen - 1);
        if (pathlen < 0) {
            return -1;
        }
        buf[pathlen++] = '/';
    }

    memcpy(buf + pathlen, name, namelen + 1); /* include trailing \0 */
    return pathlen + namelen;
}
#endif


static const char* fsobserver_getMountPoint(struct dentry *dentry) {
    struct super_block *sb = dentry->d_sb;
    struct mount *mnt;
    #if DEBUG
    struct vfsmount *vfsmnt;
    #endif
    mnt = list_first_entry(&sb->s_mounts, typeof(*mnt), mnt_instance);
    if(mnt != NULL && mnt->mnt_mountpoint != NULL) {
         #if DEBUG
         vfsmnt = &(mnt->mnt);
         if(vfsmnt != NULL) {
             printk("mount point:%s dentry:%s mnt_root:%s\n", mnt->mnt_mountpoint->d_name.name, dentry->d_name.name, vfsmnt->mnt_root->d_name.name);
         }
         #endif
         return mnt->mnt_mountpoint->d_name.name;
    }
    return NULL;

}


static size_t fsobserver_getDentryFullPath(char* path, struct dentry *dentry, char* buf, size_t buffSize) {
    const char* mount_point = "";
    int path_len = 0;

    memset(buf, 0, buffSize);
    mount_point = fsobserver_getMountPoint(dentry);
    if(strcmp(mount_point, "emulated") == 0) {
        snprintf(buf, buffSize, "%s/%s", EXTERNAL_STORAGE_PREFIX, mount_point);
    } else {
        if(!(strcmp(mount_point, "first_stage_ramdisk") == 0)) {
            snprintf(buf, buffSize, "/%s", mount_point);
        }
    }
    path_len = strlen(buf);
    if(get_node_path_locked(dentry, buf + path_len, buffSize - path_len) <= 0) {
        return 0;
    }
    //printk("sub path:%s\n", buf+mount_point_len-1);
    if(path != NULL) {
        if( strncmp(buf, "///", 3) == 0) {
            memset(buf, 0, buffSize);
            snprintf(buf, buffSize, "%s", path);
        }
    }

    return strlen(buf);
}

static size_t  fsobserver_createMsg(FO_OP op, struct inode *inode, struct dentry *dentry,
                                                         uid_t cuid, pid_t cpid, char* buf, size_t buffSize,
                                                         struct dentry *old_dentry, struct dentry *new_dentry, char* path) {
    int len = 0;
    char*  offset = NULL;
    int fmCnt = 0;
    FO_Header* header  = NULL;
    FO_FragMent *fm_ino = NULL, *fm_size =NULL, *fm_uid=NULL,
                         *fm_pid=NULL, *fm_tname=NULL, *fm_pname=NULL,
                         *fm_cuid=NULL, *fm_cpid=NULL, *fm_path = NULL,
                         *fm_oldpath=NULL, *fm_newpath=NULL;
    struct task_struct *task = current;


        memset(buf, 0, buffSize);
        offset = buf;
        header = (FO_Header*)offset;
        header->flag[0] = 'F';
        header->flag[1] = 'O';
        header->flag[2] = 'M';
        header->flag[3] = 'G';
        header->op = op;

        offset += sizeof(FO_Header);
        #if DEBUG
        printk("buf:%p  offset:%p, header:%p\n", buf, offset, header);
        #endif


        fm_ino = (FO_FragMent*)offset;
        fm_ino->fm = FRAGEMENT_INO;
        offset += sizeof(FO_FragMent);
        fmCnt++;

        fm_size = (FO_FragMent*)offset;
        fm_size->fm = FRAGEMENT_SIZE;
        offset += sizeof(FO_FragMent);
        fmCnt++;

        fm_uid = (FO_FragMent*)offset;
        fm_uid->fm = FRAGEMENT_UID;
        offset += sizeof(FO_FragMent);
        fmCnt++;

        fm_pid = (FO_FragMent*)offset;
        fm_pid->fm = FRAGEMENT_PID;
        offset += sizeof(FO_FragMent);
        fmCnt++;

        if(cuid != 0) {
            fm_cuid = (FO_FragMent*)offset;
            fm_cuid->fm = FRAGEMENT_CUID;
            offset += sizeof(FO_FragMent);
            fmCnt++;
        }
        if(cpid != 0) {
            fm_cpid = (FO_FragMent*)offset;
            fm_cpid->fm = FRAGEMENT_CPID;
            offset += sizeof(FO_FragMent);
            fmCnt++;
        }

        fm_tname = (FO_FragMent*)offset;
        fm_tname->fm = FRAGEMENT_TNAME;
        offset += sizeof(FO_FragMent);
        fmCnt++;

        fm_pname = (FO_FragMent*)offset;
        fm_pname->fm = FRAGEMENT_PNAME;
        offset += sizeof(FO_FragMent);
        fmCnt++;

        if(dentry != NULL) {
            fm_path = (FO_FragMent*)offset;
            fm_path->fm = FRAGEMENT_PATH;
            offset += sizeof(FO_FragMent);
            fmCnt++;
        }

        if(old_dentry != NULL) {
            fm_oldpath = (FO_FragMent*)offset;
            fm_oldpath->fm = FRAGEMENT_OLDPATH;
            offset += sizeof(FO_FragMent);
            fmCnt++;
        }

        if(new_dentry != NULL) {
            fm_newpath = (FO_FragMent*)offset;
            fm_newpath->fm = FRAGEMENT_NEWPATH;
            offset += sizeof(FO_FragMent);
            fmCnt++;
        }

        if(fm_ino != NULL) {
            if(inode == NULL) {
                memset((void*)offset, 0, sizeof( unsigned long));
            } else {
                memcpy((void*)offset, &(inode->i_ino), sizeof( unsigned long));
            }
            fm_ino->offset = offset - buf;
            fm_ino->len = sizeof(unsigned long);
            offset += fm_ino->len;
        }
        if(fm_size != NULL) {
            if(inode == NULL) {
                memset((void*)offset, 0, sizeof(loff_t));
            } else {
                memcpy((void*)offset, &(inode->i_size), sizeof(loff_t));
            }
            fm_size->offset = offset - buf;
            fm_size->len = sizeof(loff_t);
            offset += fm_size->len;
        }
        if(fm_uid != NULL) {
            memcpy((void*)offset, &(current->cred->uid.val), sizeof(kuid_t));
            fm_uid->offset = offset - buf;
            fm_uid->len = sizeof(kuid_t);
            offset += fm_uid->len;
        }
        if(fm_pid != NULL) {
            memcpy((void*)offset, &(current->tgid), sizeof(pid_t));
            fm_pid->offset = offset - buf;
            fm_pid->len = sizeof(pid_t);
            offset += fm_pid->len;
        }

        if(cuid != 0 && fm_cuid != NULL) {
            memcpy((void*)offset, &(cuid), sizeof(uid_t));
            fm_cuid->offset = offset - buf;
            fm_cuid->len = sizeof(uid_t);
            offset += fm_cuid->len;
        }
        if(cpid != 0 && fm_cpid != NULL) {
            memcpy((void*)offset, &(cpid), sizeof(pid_t));
            fm_cpid->offset = offset - buf;
            fm_cpid->len = sizeof(pid_t);
            offset += fm_cpid->len;
        }

        if(current->parent != NULL) {
            if((current->comm[0] == 'c' && current->comm[1] == 'p' && current->comm[2] == 0) ||
                (current->comm[0] == 'm' && current->comm[1] == 'v' && current->comm[2] == 0)) {
                task = current->parent;
            } else {
                task = current;
            }
        }

        if(fm_tname != NULL) {
            len = strlen(task->comm);
            memcpy((void*)offset, task->comm, len);
            fm_tname->offset = offset - buf;
            fm_tname->len = len;
            offset += fm_tname->len;
            offset++;
        }
        if(fm_pname != NULL) {
            len = get_cmdline(task, (void*)offset, buffSize-(offset-buf));
            fm_pname->offset = offset - buf;
            fm_pname->len = len;
            offset += fm_pname->len;
            offset++;
        }

        if(dentry != NULL && fm_path != NULL) {
            len = fsobserver_getDentryFullPath(path, dentry, (void*)offset, buffSize-(offset-buf));
            fm_path->offset = offset - buf;
            fm_path->len = len;
            offset += fm_path->len;
            offset++;
        }

        if(old_dentry != NULL && fm_oldpath != NULL) {
            len = fsobserver_getDentryFullPath(NULL, old_dentry, (void*)offset, buffSize-(offset-buf));
            fm_oldpath->offset = offset - buf;
            fm_oldpath->len = len;
            offset += fm_oldpath->len;
            offset++;
        }

        if(new_dentry != NULL && fm_newpath != NULL) {
            len = fsobserver_getDentryFullPath(NULL, new_dentry, (void*)offset, buffSize-(offset-buf));
            fm_newpath->offset = offset - buf;
            fm_newpath->len = len;
            offset += fm_newpath->len;
        }


        header->fmCnt = fmCnt;
        #if DEBUG
        if(fm_path != NULL) {
            printk("uid:%d pid:%d op:%d fmCnt:%d  ino:%lu pname:%s path:%s", current->cred->uid.val, current->tgid, header->op, header->fmCnt, *(unsigned long*)(buf+fm_ino->offset),  buf+fm_pname->offset, buf+fm_path->offset);
        } else if(fm_oldpath != NULL && fm_newpath != NULL) {
            printk("uid:%d pid:%d op:%d fmCnt:%d  ino:%lu pname:%s oldpath:%s  newpath:%s", current->cred->uid.val, current->tgid, header->op, header->fmCnt, *(unsigned long*)(buf+fm_ino->offset),  buf+fm_pname->offset, buf+fm_oldpath->offset, buf+fm_newpath->offset);
        }
        #endif

        return offset-buf;
    return 0;
}


int fsobserver_post_create(struct dentry *dentry)  {
    char* data;
    size_t len = 0;

    if(dentry == NULL ||  dentry->d_inode == NULL) {
        return -1;
    }

    if(!S_ISREG(dentry->d_inode->i_mode)) {
        return -1;
    }

    if(!isAllow(dentry, NULL, NULL)) {
        return -1;
    }

    #if DEBUG
    printk("fsobserver create file:%s uid:%d pid:%d", dentry->d_name.name, current->cred->uid.val, current->tgid);
    #if DEBUG_DUMP_STACK
    dump_stack();
    #endif
    #endif

    data = (char*)kmalloc(NL_MSG_MAX, GFP_KERNEL);
    if(data != NULL) {
        len = fsobserver_createMsg(OP_CREATE, dentry->d_inode, dentry, 0, 0, data, NL_MSG_MAX, NULL, NULL, NULL);
        fsobserver_nl_send_msg(data, len);
        kfree(data);
    }

    return 0;

}

int fsobserver_post_read(struct file *file)  {
    char* data;
    size_t len = 0;
    int cmdline_len = 0;
    struct super_block *sb = NULL;
    struct file_system_type	*s_type = NULL;
    char* dstPath=NULL;
    char* tmpPath = NULL;
    char* cmdline = NULL;

    if(!gEnable)
        return 0;

    if(!gEnableRead) {
        return 0;
    }

    if(g_pid == -1)
        return -1;

    if(file == NULL) {
        return -1;
    }

    //only interested with file accessing of root and system user
    if(0 != current->cred->uid.val && 1000 != current->cred->uid.val) {
        return 0;
    }


    if(current->tgid < 550) {
        return 0;
    }

    if(file->f_path.dentry == NULL) {
        return -1;
    }
    if( (file->android_vendor_data1 & FSOBSERVER_FS_MASK) &  FSOBSERVER_FS_FIRST_READ ) {
        return 0;
    }

    sb = file->f_path.dentry->d_sb;
    if(sb == NULL) {
        return -1;
    }
    s_type = sb->s_type;
    if(s_type  == NULL) {
        return -1;
    }
    if(! ((strcmp(s_type->name, WATCH_FSTYPE_F2FS) == 0) ||
            (strcmp(s_type->name, WATCH_FSTYPE_OVERLAYFS) == 0) ||
            (strcmp(s_type->name, WATCH_FSTYPE_EXT4FS) == 0))){
        return 0;
    }

    cmdline = (char*)kmalloc(CMD_LINE_MAX, GFP_KERNEL);
    cmdline_len = get_cmdline(current, (void*)cmdline, CMD_LINE_MAX);
    if( ! ((strncmp(cmdline, "system_server", 13) == 0) ||
            (strncmp(cmdline, "zygote64", 6) == 0) ||
            (strncmp(cmdline, "com.android.systemui", 20) == 0))){
        if(cmdline != NULL) {
            kfree(cmdline);
        }
        return 0;
    }
    if(cmdline != NULL) {
        kfree(cmdline);
    }

    data = (char*)kmalloc(NL_MSG_MAX, GFP_KERNEL);
    if(data != NULL) {
        tmpPath = (char*)kmalloc(4096, GFP_KERNEL);
        if(tmpPath != NULL) {
            dstPath = d_path(&(file->f_path), tmpPath, 4096);
        }
        #if DEBUG
        printk("fsobserver file (d_path):s_type:%s uid:%d pid:%d path:%s",  s_type->name, current->cred->uid.val, current->tgid, dstPath);
        #endif


        len = fsobserver_createMsg(OP_FIRST_READ, NULL, file->f_path.dentry, 0, 0, data, NL_MSG_MAX, NULL, NULL, dstPath);
        fsobserver_nl_send_msg(data, len);
        if(tmpPath != NULL) {
            kfree(tmpPath);
        }
        kfree(data);
    }

    file->android_vendor_data1 |= FSOBSERVER_FS_FIRST_READ;
    return 0;
}


/*
int fsobserver_post_open(struct file *file)  {
    if(file == NULL || file->f_path.dentry == NULL ||
        file->f_path.dentry->d_inode == NULL) {
        printk("post_open err1\n");
        return -1;
    }

    if(!S_ISREG(file->f_path.dentry->d_inode->i_mode)) {
        printk("post_open err2\n");
        return -1;
    }

    if(!isAllow(NULL, NULL, file)) {
        return -1;
    }

    #if DEBUG
    if(file->f_path.dentry != NULL) {
        printk("fsobserver open file:%s uid:%d pid:%d", file->f_path.dentry->d_name.name, current->cred->uid.val, current->tgid);
    } else {
        printk("fsobserver open file (dentry is null) uid:%d pid:%d", current->cred->uid.val, current->tgid);
    }
    #if DEBUG_DUMP_STACK
    dump_stack();
    #endif

    #endif

    file->observer_data = kzalloc(sizeof(struct fsobserver_file_info), GFP_KERNEL);
    #if DEBUG
    printk("post_file_open file:%p  observer_data:%p  ino: %lu  \n",file, file->observer_data, file->f_path.dentry->d_inode->i_ino);
    #endif
    return 0;

}
*/

int fsobserver_post_write(struct file *file)  {
    if(!gEnable)
        return 0;

    if(g_pid == -1)
        return -1;

    if(file == NULL) {
        return -1;
    }

    file->android_vendor_data1 |= FSOBSERVER_FS_MODIFY;


    #if 0
    if(file->f_path.dentry != NULL && strstr(file->f_path.dentry->d_name.name, "fstest_") != NULL) {
        printk("fsobserver post file write,file(%p) name:%s  uid:%d  pid:%d \n", file, file->f_path.dentry->d_name.name, current->cred->uid.val, current->tgid);
    }
    #endif

    return 0;
}
int fsobserver_post_mkdir(struct inode *dir, struct dentry *dentry) {
    char* data;
    size_t len = 0;

    if(!isAllow(dentry, dir, NULL)) {
        return -1;
    }

    #if DEBUG
    printk("fsobserver rmdir file:%s uid:%d pid:%d", dentry->d_name.name, current->cred->uid.val, current->tgid);
    #if DEBUG_DUMP_STACK
    dump_stack();
    #endif
    #endif

     if(dentry != NULL && dir != NULL) {
        data = (char*)kmalloc(NL_MSG_MAX, GFP_KERNEL);
        if(data != NULL) {
            len = fsobserver_createMsg(OP_MKDIR, dir, dentry, 0, 0, data, NL_MSG_MAX, NULL, NULL, NULL);
            fsobserver_nl_send_msg(data, len);
            kfree(data);
        }
    }
    return 0;
}
int fsobserver_post_rmdir(struct inode *dir, struct dentry *dentry) {
    char* data;
    size_t len = 0;

    if(!isAllow(dentry, dir, NULL)) {
        return -1;
    }

    #if DEBUG
    printk("fsobserver rmdir file:%s uid:%d pid:%d", dentry->d_name.name, current->cred->uid.val, current->tgid);
    #if DEBUG_DUMP_STACK
    dump_stack();
    #endif
    #endif

    if(dentry != NULL) {
        data = (char*)kmalloc(NL_MSG_MAX, GFP_KERNEL);
        if(data != NULL) {
            len = fsobserver_createMsg(OP_RMDIR, dentry->d_inode, dentry, 0, 0, data, NL_MSG_MAX, NULL, NULL, NULL);
            fsobserver_nl_send_msg(data, len);
            kfree(data);
        }
    }
    return 0;
}
int fsobserver_post_rename(struct inode *old_dir, struct dentry *old_dentry,
             struct inode *new_dir, struct dentry *new_dentry) {
    char* data;
    size_t len = 0;

    if(!isAllow(new_dentry, new_dir, NULL)) {
        return -1;
    }

     #if DEBUG
     printk("fsobserver rename file:%s to %s uid:%d pid:%d\n", old_dentry->d_name.name, new_dentry->d_name.name, current->cred->uid.val, current->tgid);
     #if DEBUG_DUMP_STACK
     dump_stack();
     #endif
     #endif

     if(old_dir != NULL && old_dentry != NULL && new_dir != NULL && new_dentry != NULL) {
        data = (char*)kmalloc(NL_RNAME_MSG_MAX, GFP_KERNEL);
        if(data != NULL) {
            len = fsobserver_createMsg(OP_RENAME, old_dentry->d_inode, NULL, 0, 0, data,NL_MSG_MAX, old_dentry, new_dentry, NULL);
            fsobserver_nl_send_msg(data, len);
            kfree(data);
        }
    }

     return 0;
}

int fsobserver_post_unlink(struct inode *dir, struct dentry *dentry) {
    char* data;
    size_t len = 0;

    if(!isAllow(dentry, dir, NULL)) {
        return -1;
    }

    #if DEBUG
    printk("fsobserver unlink file:%s uid:%d pid:%d", dentry->d_name.name, current->cred->uid.val, current->tgid);
    #if DEBUG_DUMP_STACK
    dump_stack();
    #endif
    #endif

    if( dentry != NULL ) {
        data = (char*)kmalloc(NL_MSG_MAX, GFP_KERNEL);
        if(data != NULL) {
            len = fsobserver_createMsg(OP_UNLINK, dentry->d_inode, dentry, 0, 0, data, NL_MSG_MAX, NULL, NULL, NULL);
            fsobserver_nl_send_msg(data, len);
            kfree(data);
        }
    }

    return 0;
}

int fsobserver_post_release(struct inode *inode, struct file *file) {
    char* data;
    size_t len = 0;

    if(!isAllow(NULL, inode, file)) {
        return -1;
    }

    #if DEBUG
    if(file->f_path.dentry != NULL) {
        printk("fsobserver release (%p) file:%s uid:%d pid:%d", file, file->f_path.dentry->d_name.name, current->cred->uid.val, current->tgid);
    } else {
        printk("fsobserver release (%p) file:(dentry is null) uid:%d pid:%d", file, current->cred->uid.val, current->tgid);
    }
    #endif

    if(inode == NULL) {
        #if DEBUG
        printk("fsobserver release file inode is null!\n");
        #endif
        return -1;
    }

    if(inode != NULL) {
        if(!S_ISREG(inode->i_mode)) {
            return 0;
        }
    }

    #if DEBUG
    if(file->f_path.dentry != NULL) {
        printk("fsobserver file name:%s ino:%lu modified:%d", file->f_path.dentry->d_name.name, inode->i_ino, ( (file->android_vendor_data1 & FSOBSERVER_FS_MASK )  & FSOBSERVER_FS_MODIFY));
    } else {
        printk("fsobserver file ino:%lu modified:%d", inode->i_ino, ( (file->android_vendor_data1 & FSOBSERVER_FS_MASK)  & FSOBSERVER_FS_MODIFY));
    }
    #endif


    if(file != NULL) {
        if(file->f_path.dentry != NULL &&  (( (file->android_vendor_data1 & FSOBSERVER_FS_MASK) & FSOBSERVER_FS_MODIFY) )) {
            data = (char*)kmalloc(NL_MSG_MAX, GFP_KERNEL);
            if(data != NULL) {
                #if 1
                if(inode->i_ino == g_last_msg.ino && ((jiffies - g_last_msg.time) < HZ)) {
                    memset(&g_last_msg.data, 0, NL_MSG_MAX);
                    g_last_msg.data_len = fsobserver_createMsg(OP_CLOSE_WRITE, inode, file->f_path.dentry, 0/*file->f_cuid*/, 0/*file->f_cpid*/, g_last_msg.data, NL_MSG_MAX, NULL, NULL, NULL);
                    g_last_msg.is_valid = true;

                    g_last_msg.ino = inode->i_ino;
                    g_last_msg.time = jiffies;
                } else {
                    if(g_last_msg.is_valid) {
                        fsobserver_nl_send_msg(g_last_msg.data, g_last_msg.data_len);
                        g_last_msg.is_valid = false;
                    }

                    g_last_msg.ino = inode->i_ino;
                    g_last_msg.time = jiffies;
                    len = fsobserver_createMsg(OP_CLOSE_WRITE, inode, file->f_path.dentry, /*file->f_cuid*/0, /*file->f_cpid*/0, data, NL_MSG_MAX, NULL, NULL, NULL);
                    fsobserver_nl_send_msg(data, len);
                }
                #else
                len = fsobserver_createMsg(OP_CLOSE_WRITE, inode, file->f_path.dentry, &ff->creator, data, NL_MSG_MAX, NULL, NULL, NULL);
                fsobserver_nl_send_msg(data, len);
                #endif
                kfree(data);
            }
        }
    }
    return 0;
}


bool fsobserver_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
       void __user *ubuf = (void __user *)arg;
       struct fsobserver_file_creator tcreator;

       if(file == NULL) {
           return false;
       }
       if(!isAllow(NULL, NULL, file)) {
            return -1;
        }

        #if DEBUG
        printk("SDCARDFS ioctl be called  cmd:%u\n", cmd);
        #if DEBUG_DUMP_STACK
        dump_stack();
        #endif
        #endif

        if(cmd == FSOBSERVER_FILEOBSERVER_IOCTL_CMD) {
            if (copy_from_user(&tcreator, ubuf, sizeof(struct fsobserver_file_creator))) {
                printk("copy from user error!\n");
                return false;
            } else {
                #if DEBUG
                printk("SDCARDFS receive ioctl uid:%d, pid:%d\n", tcreator.uid, tcreator.pid);
                #endif
                //file->f_cuid = tcreator.uid;
                //file->f_cpid = tcreator.pid;
            }
            return true;
        }
        return false;
}
//Nubia File Observer End

#endif //end #ifdef ENABLE_FILE_OBSERVER
