/******************************************************************************
 *  KERNEL HEADER
 ******************************************************************************/
#include <mach/sec_osal.h>

#include <linux/string.h>
#include <linux/bug.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/mtd/mtd.h>
#include <linux/fs.h>
#include <linux/mtd/partitions.h>
#include <asm/uaccess.h>
#include <linux/slab.h> 
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
#include <linux/mtd/nand.h>
#endif

/*****************************************************************************
 * MACRO
 *****************************************************************************/
#ifndef ASSERT
    #define ASSERT(expr)        BUG_ON(!(expr))
#endif

/*****************************************************************************
 * GLOBAL VARIABLE
 *****************************************************************************/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36))
    DECLARE_MUTEX(sej_sem);           
    DECLARE_MUTEX(mtd_sem);           
    DECLARE_MUTEX(rid_sem);             
    DECLARE_MUTEX(sec_mm_sem);         
    DECLARE_MUTEX(osal_fp_sem);                      
#else // (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36))        
    DEFINE_SEMAPHORE(sej_sem);
    DEFINE_SEMAPHORE(mtd_sem);
    DEFINE_SEMAPHORE(rid_sem);    
    DEFINE_SEMAPHORE(sec_mm_sem);   
    DEFINE_SEMAPHORE(osal_fp_sem);            
#endif

/*****************************************************************************
 * LOCAL VARIABLE
 *****************************************************************************/
static mm_segment_t                 curr_fs;
#define OSAL_MAX_FP_COUNT           30000
#define OSAL_FP_OVERFLOW            OSAL_MAX_FP_COUNT
/* The array 0 will be not be used, and fp_id=0 will be though as NULL file */
static struct file *g_osal_fp[OSAL_MAX_FP_COUNT] = {0};

/*****************************************************************************
 * PORTING LAYER
 *****************************************************************************/
void osal_kfree(void *buf)
{
    kfree(buf);
}

void* osal_kmalloc(unsigned int size)
{
    return kmalloc(size,GFP_KERNEL);
}

unsigned long osal_copy_from_user(void * to, void * from, unsigned long size)
{
    return copy_from_user(to, from, size);
}

unsigned long osal_copy_to_user(void * to, void * from, unsigned long size)
{
    return copy_to_user(to, from, size);
}

int osal_sej_lock(void)
{
    return down_interruptible(&sej_sem);
}

void osal_sej_unlock(void)
{
    up(&sej_sem);
}

int osal_mtd_lock(void)
{
    return down_interruptible(&mtd_sem);
}

void osal_mtd_unlock(void)
{
    up(&mtd_sem);
}

int osal_rid_lock(void)
{
    return down_interruptible(&rid_sem);
}

void osal_rid_unlock(void)
{
    up(&rid_sem);
}

void osal_msleep(unsigned int msec)
{
    msleep(msec);
}

void osal_assert(unsigned int val)
{
    ASSERT(val);
}

int osal_set_kernel_fs (void)
{
    int val = 0;
    val = down_interruptible(&sec_mm_sem);
    curr_fs = get_fs();    
    set_fs(KERNEL_DS);
    return val;
}

void osal_restore_fs (void)
{
    set_fs(curr_fs);
    up(&sec_mm_sem);
}

static int osal_filp_wrapper_get_empty_index(void)
{
    int i = 0;
    int val = 0;
    
    val = down_interruptible(&osal_fp_sem);

    for(i=1 ; i<OSAL_MAX_FP_COUNT; i++)
    {
        if(g_osal_fp[i] == NULL)
        {
            break;
        }
    }
    
    up(&osal_fp_sem);

    return i;
}

int osal_filp_open_read_only(const char *file_path)
{
    int filp_id = osal_filp_wrapper_get_empty_index();
    int val = 0;
    
    if(filp_id != OSAL_FP_OVERFLOW)
    {
        val = down_interruptible(&osal_fp_sem);
        
        g_osal_fp[filp_id] = filp_open(file_path, O_RDONLY, 0777);
        
        up(&osal_fp_sem);
        
        return filp_id;
    }

    /* the fp_id = 0 will be thought as NULL file ponter */
    return OSAL_FILE_NULL;
}

void* osal_get_filp_struct(int fp_id)
{
    int val = 0;
    struct file *ret;
    
    if(fp_id >= 1 && fp_id < OSAL_MAX_FP_COUNT)
    {
        val = down_interruptible(&osal_fp_sem);
        
        ret = g_osal_fp[fp_id];
        
        up(&osal_fp_sem);
        
        return (void*)ret;
    }

    return NULL;
}

int osal_filp_close(int fp_id)
{
    int val = 0;
    int ret;
    
    if(fp_id >= 1 && fp_id < OSAL_MAX_FP_COUNT)
    {
        val = down_interruptible(&osal_fp_sem);
        
        ret = filp_close(g_osal_fp[fp_id], NULL);
        g_osal_fp[fp_id] = NULL;
        
        up(&osal_fp_sem);
        
        return ret;
    }

    return OSAL_FILE_CLOSE_FAIL;
}

loff_t osal_filp_seek_set(int fp_id, loff_t off)
{
    loff_t offset;
    int val = 0;

    if(fp_id >= 1 && fp_id < OSAL_MAX_FP_COUNT)
    {
        val = down_interruptible(&osal_fp_sem);

        offset = g_osal_fp[fp_id]->f_op->llseek(g_osal_fp[fp_id],off,SEEK_SET);
    
        up(&osal_fp_sem);
        
        return offset;
    }

    return OSAL_FILE_SEEK_FAIL;
}

loff_t osal_filp_seek_end(int fp_id, loff_t off)
{
    loff_t offset;
    int val = 0;

    if(fp_id >= 1 && fp_id < OSAL_MAX_FP_COUNT)
    {
        val = down_interruptible(&osal_fp_sem);

        offset = g_osal_fp[fp_id]->f_op->llseek(g_osal_fp[fp_id],off,SEEK_END);
    
        up(&osal_fp_sem);
        
        return offset;
    }

    return OSAL_FILE_SEEK_FAIL;
}

loff_t osal_filp_pos(int fp_id)
{
    loff_t offset;
    int val = 0;
    
    if(fp_id >= 1 && fp_id < OSAL_MAX_FP_COUNT)
    {
        val = down_interruptible(&osal_fp_sem);

        offset = g_osal_fp[fp_id]->f_pos;
            
        up(&osal_fp_sem);
        
        return offset;
    }

    return OSAL_FILE_GET_POS_FAIL;
}

long osal_filp_read(int fp_id, char *buf, unsigned long len)
{
    ssize_t read_len;
    int val = 0;
    
    if(fp_id >= 1 && fp_id < OSAL_MAX_FP_COUNT)
    {
        val = down_interruptible(&osal_fp_sem);

        read_len = g_osal_fp[fp_id]->f_op->read(g_osal_fp[fp_id], buf, len, &g_osal_fp[fp_id]->f_pos);
            
        up(&osal_fp_sem);
        
        return read_len;
    }
    
    return OSAL_FILE_READ_FAIL;
}

long osal_is_err(int fp_id)
{
    bool err;
    int val = 0;
    
    if(fp_id >= 1 && fp_id < OSAL_MAX_FP_COUNT)
    {
        val = down_interruptible(&osal_fp_sem);

        err = IS_ERR(g_osal_fp[fp_id]);
            
        up(&osal_fp_sem);
        
        return err;
    }    
    
    osal_assert(0);   
    return 1;
}

EXPORT_SYMBOL(osal_kfree);
EXPORT_SYMBOL(osal_kmalloc);
EXPORT_SYMBOL(osal_copy_from_user);
EXPORT_SYMBOL(osal_copy_to_user);
EXPORT_SYMBOL(osal_sej_lock);
EXPORT_SYMBOL(osal_sej_unlock);
EXPORT_SYMBOL(osal_mtd_lock);
EXPORT_SYMBOL(osal_mtd_unlock);
EXPORT_SYMBOL(osal_rid_lock);
EXPORT_SYMBOL(osal_rid_unlock);
EXPORT_SYMBOL(osal_msleep);
EXPORT_SYMBOL(osal_assert);
EXPORT_SYMBOL(osal_set_kernel_fs);
EXPORT_SYMBOL(osal_restore_fs);
EXPORT_SYMBOL(osal_get_filp_struct);
EXPORT_SYMBOL(osal_filp_close);
EXPORT_SYMBOL(osal_filp_seek_set);
EXPORT_SYMBOL(osal_filp_seek_end);
EXPORT_SYMBOL(osal_filp_pos);
EXPORT_SYMBOL(osal_filp_read);
EXPORT_SYMBOL(osal_is_err);
EXPORT_SYMBOL(osal_filp_open_read_only);
