#include "cap_fs.h"

#include "altc/string.h"
#include "cap_ops.h"
#include "cap_table.h"
#include "cap_util.h"
#include "error.h"
#include "ff.h"
#include "proc.h"

#define METADATA_DIR_ENTRY_SIZE (32) /* FROM FatFS SZDIRE */

#include <stdint.h>

// In addition to the cap_table we need to reliably track parent relationships,
// otherwise revocation could touch unrelated capabilities due to how the CDT works
// (CTE next, prev pointers only)
typedef struct {
	uint32_t parent;
	uint32_t next_sibling;
	uint32_t first_child;
	char path[S3K_MAX_PATH_LEN];
	bool occupied;
} tree_node_t;

// Each node is identified by its tag, the offset on the nodes array.
static uint32_t current_idx = 0;
static tree_node_t nodes[S3K_MAX_PATH_CAPS] = {
  // Needs to be kept in sync with INIT_CAPS
    [0] = {
	   .parent = 0,
	   .first_child = 0,
	   .next_sibling = 0,
	   .path = "/",
	   .occupied = true,
	   }
};
_Static_assert(sizeof(nodes) <= (1 << 14)); /* Not more than 16 KiB */

static FATFS FatFs; /* FatFs work area needed for each volume */

char *fresult_get_error(FRESULT fr)
{
	switch (fr) {
	case FR_OK:
		return "(0) Succeeded";
	case FR_DISK_ERR:
		return "(1) A hard error occurred in the low level disk I/O layer";
	case FR_INT_ERR:
		return "(2) Assertion failed";
	case FR_NOT_READY:
		return "(3) The physical drive cannot work";
	case FR_NO_FILE:
		return "(4) Could not find the file";
	case FR_NO_PATH:
		return "(5) Could not find the path";
	case FR_INVALID_NAME:
		return "(6) The path name format is invalid";
	case FR_DENIED:
		return "(7) Access denied due to prohibited access or directory full";
	case FR_EXIST:
		return "(8) Access denied due to prohibited access";
	case FR_INVALID_OBJECT:
		return "(9) The file/directory object is invalid";
	case FR_WRITE_PROTECTED:
		return "(10) The physical drive is write protected";
	case FR_INVALID_DRIVE:
		return "(11) The logical drive number is invalid";
	case FR_NOT_ENABLED:
		return "(12) The volume has no work area";
	case FR_NO_FILESYSTEM:
		return "(13) There is no valid FAT volume";
	case FR_MKFS_ABORTED:
		return "(14) The f_mkfs() aborted due to any problem";
	case FR_TIMEOUT:
		return "(15) Could not get a grant to access the volume within defined period";
	case FR_LOCKED:
		return "(16) The operation is rejected according to the file sharing policy";
	case FR_NOT_ENOUGH_CORE:
		return "(17) LFN working buffer could not be allocated";
	case FR_TOO_MANY_OPEN_FILES:
		return "(18) Number of open files > FF_FS_LOCK";
	case FR_INVALID_PARAMETER:
		return "(19) Given parameter is invalid";
	default:
		return "(XX) Unknown";
	}
}

/**
 * Debugging facility
*/
__attribute__((unused)) static void dump_tree(uint32_t tag, int depth)
{
	if (!nodes[tag].occupied) {
		// Handle empty or unoccupied nodes (optional)
		alt_printf("Unoccupied node, BUG!!!\n");
		return;
	}

	for (int i = 0; i < depth; i++) {
		alt_putchar('\t');
	}

	tree_node_t *n = &nodes[tag];
	alt_printf("Node %d: %s, parent=%d, occupied=%d, first_child=%d, next_sibling=%d\n", tag,
		   n->path, n->parent, n->occupied, n->first_child, n->next_sibling);
	alt_printf("Node %d: %s, parent=%d, occupied=%d, first_child=%d, next_sibling=%d\n", tag,
		   n->path, n->parent, n->occupied, n->first_child, n->next_sibling);

	// Recursively dump children
	uint32_t child = n->first_child;
	while (child) {
		dump_tree(child, depth + 1);
		child = nodes[child].next_sibling;
	}
}

void fs_init()
{
	FRESULT fr;
	fr = f_mount(
	    &FatFs, "",
	    1 /* OPT = 1 -> mount immediately*/); /* Give a work area to the default drive */
	if (fr == FR_OK) {
		alt_puts("File system mounted OK");
	} else {
		alt_printf("File system not mounted: %s\n", fresult_get_error(fr));
	}

	DWORD fre_clust, fre_sect, tot_sect;

	FATFS *fs;

	/* Get volume information and free clusters of drive 0 */
	FRESULT res = f_getfree("", &fre_clust, &fs);
	if (res == FR_OK) {
		alt_puts("File system free space retrieved OK");
	} else {
		alt_printf("File system free space not retrieved: %s\n", fresult_get_error(fr));
	}

	/* Get total sectors and free sectors */
	tot_sect = (fs->n_fatent - 2) * fs->csize;
	fre_sect = fre_clust * fs->csize;

	/* Print the free space (assuming 512 bytes/sector) */
	alt_printf("%d B total drive space.\n%d B available.\n", tot_sect * 512, fre_sect * 512);

	uint32_t free_bytes = fre_sect * 512;

	// Find the initial path capability and set its capability to the fre size
	for (size_t i = 0; i < S3K_CAP_CNT; i++) {
		// Boot PID / 0 PID
		cte_t c = ctable_get(0, i);
		cap_t cap = cte_cap(c);
		if (cap.type == CAPTY_PATH) {
			cap.path.create_quota = free_bytes;
			cte_set_cap(c, cap);
			return;
		}
	}
}

uint32_t find_next_free_idx()
{
	uint32_t idx = current_idx;
	while (nodes[idx].occupied) {
		idx = (idx + 1) % S3K_MAX_PATH_CAPS;
		// If we have gone a full cycle without finding a free tag
		if (idx == current_idx)
			return -1;
	}
	return idx;
}

err_t path_read(cap_t path, char *buf, size_t n)
{
	if (!path.type)
		return ERR_EMPTY;
	if (path.type != CAPTY_PATH)
		return ERR_INVALID_PATH;
	ssize_t ret = strscpy(buf, nodes[path.path.tag].path, n);
	if (ret < 0)
		return ERR_PATH_TOO_LONG;
	return SUCCESS;
}

err_t path_derive(cte_t src, cte_t dst, const char *path, uint32_t create_quota, path_flags_t flags)
{
	cap_t scap = cte_cap(src);
	if (!scap.type)
		return ERR_SRC_EMPTY;

	if (cte_cap(dst).type)
		return ERR_DST_OCCUPIED;

	if (scap.type != CAPTY_PATH)
		return ERR_INVALID_DERIVATION;

	// Cannot derive file to directory
	if (((flags & FILE) == FILE) < scap.path.file)
		return ERR_INVALID_DERIVATION;
	// Cannot elevate read or write privilege
	if ((((flags & PATH_READ) == PATH_READ) && !scap.path.read)
	    || (((flags & PATH_WRITE) == PATH_WRITE) && !scap.path.write))
		return ERR_INVALID_DERIVATION;

	// Can only derive file to file when not using a new path i.e NULL pointer (0)
	if (scap.path.file && path)
		return ERR_INVALID_DERIVATION;

	// Check no relative paths to cheat the isolation.
	if (path) {
		char *res = alt_strstr(path, "..");
		if (res) {
			return ERR_INVALID_PATH;
		}
	}

	// Space can not exceed current space quota
	if ((flags & PATH_WRITE) && scap.path.create_quota < create_quota)
		return ERR_INVALID_DERIVATION;

	// Can the system handle more path storage?
	int new_idx = find_next_free_idx();
	if (new_idx == -1) {
		return ERR_NO_PATH_TAG;
	}

	tree_node_t *src_node = &nodes[scap.path.tag];
	tree_node_t *dest_node = &nodes[new_idx];
	*dest_node = (tree_node_t){
	    .parent = scap.path.tag,
	    // Newly derived, no children
	    .first_child = 0,
	    // If our parent already had a child, use it as our sibling, we then
	    // overwrite first child of parent below. If no existing child, we will copy
	    // 0 which will be the NULL sibling.
	    .next_sibling = src_node->first_child,
	    // Path is strscpy'd below after
	    // Occupied only updated after we are sure to finish the derivation
	};

	// Copy path
	strscpy(nodes[new_idx].path, src_node->path, S3K_MAX_PATH_LEN);
	if (path) {
		// Append path separator and new path
		ssize_t ret = strlcat(nodes[new_idx].path, "/", S3K_MAX_PATH_LEN);
		if (ret < 0)
			return ERR_PATH_TOO_LONG;
		ret = strlcat(nodes[new_idx].path, path, S3K_MAX_PATH_LEN);
		if (ret < 0)
			return ERR_PATH_TOO_LONG;
	}
	if (!(flags & PATH_WRITE)) {
		// A non-write path capability would never be able
		// to create data, so create quota is automatically zero
		create_quota = 0;
	}
	cap_t ncap = cap_mk_path(new_idx, create_quota, flags);
	cte_insert(dst, ncap, src);
	src_node->first_child = new_idx;
	scap.path.create_quota -= create_quota;
	cte_set_cap(src, scap);

	// Finish
	current_idx = new_idx;
	dest_node->occupied = true;

	return SUCCESS;
}

bool cap_path_revokable(cap_t p, cap_t c)
{
	return (c.type == CAPTY_PATH) && nodes[c.path.tag].parent == p.path.tag;
}

err_t read_file(cap_t path, uint32_t offset, uint8_t *buf, uint32_t buf_size, uint32_t *bytes_read)
{
	if (path.path.type != CAPTY_PATH || !path.path.file || !path.path.read)
		return ERR_INVALID_INDEX;

	FIL Fil; /* File object needed for each open file */
	FRESULT fr;
	err_t err = SUCCESS;

	fr = f_open(&Fil, nodes[path.path.tag].path, FA_READ);
	if (fr != FR_OK) {
		alt_printf("FF error: %s\n", fresult_get_error(fr));
		err = ERR_FILE_OPEN;
		goto ret;
	}
	fr = f_lseek(&Fil, offset);
	if (fr != FR_OK) {
		alt_printf("FF error: %s\n", fresult_get_error(fr));
		err = ERR_FILE_SEEK;
		goto cleanup;
	}
	fr = f_read(&Fil, buf, buf_size, bytes_read);
	if (fr != FR_OK) {
		alt_printf("FF error: %s\n", fresult_get_error(fr));
		err = ERR_FILE_READ;
		goto cleanup;
	}
cleanup:
	f_close(&Fil);
ret:
	return err;
}

err_t read_dir(cap_t path, size_t dir_entry_idx, dir_entry_info_t *out)
{
	FILINFO fi;
	DIR di;
	err_t err = SUCCESS;
	FRESULT fr = f_opendir(&di, nodes[path.path.tag].path);
	if (fr != FR_OK) {
		err = ERR_FILE_OPEN;
		goto out;
	}
	for (size_t i = 0; i <= dir_entry_idx; i++) {
		fr = f_readdir(&di, &fi);
		if (fr != FR_OK) {
			err = ERR_FILE_SEEK;
			goto cleanup;
		}
		// End of directory
		if (fi.fname[0] == 0) {
			err = ERR_INVALID_INDEX;
			goto cleanup;
		}
	}
	// Could do one larger memcpy here, but not certain FatFS file info and S3K
	// file info will continue to stay in sync, so leverage the type safety of
	// explicit structure assignment.
	out->fattrib = fi.fattrib;
	out->fdate = fi.fdate;
	out->fsize = fi.fsize;
	out->ftime = fi.ftime;
	memcpy(out->fname, fi.fname, sizeof(fi.fname));
cleanup:
	f_closedir(&di);
out:
	return err;
}

static FRESULT f_disk_usage(char *path, uint32_t *sum)
{
	FRESULT res;
	DIR dir;
	UINT i;
	static FILINFO fno;
	res = f_opendir(&dir, path); /* Open the directory */
	if (res == FR_OK) {
		for (;;) {
			res = f_readdir(&dir, &fno); /* Read a directory item */
			if (res != FR_OK || fno.fname[0] == 0)
				break; /* Break on error or end of dir */
			// Each directory entry takes metadata equal to the size of FILINFO.
			*sum += METADATA_DIR_ENTRY_SIZE;
			if (fno.fattrib & AM_DIR) { /* It is a directory */
				i = alt_strlen(path);
				// FIXME: could be nicer with a snprintf_s(...) or similar here
				ssize_t ret = strlcat(&path[i], "/", S3K_MAX_PATH_LEN - i);
				if (ret < 0) {
					res = FR_NOT_ENOUGH_CORE;
					break;
				}
				ret = strlcat(&path[i + 1], fno.fname, S3K_MAX_PATH_LEN - i - 1);
				if (ret < 0) {
					res = FR_NOT_ENOUGH_CORE;
					break;
				}
				res = f_disk_usage(path, sum); /* Enter the directory */
				if (res != FR_OK)
					break;
				path[i] = 0;
			} else { /* It is a file. */
				*sum += fno.fsize;
			}
		}
		f_closedir(&dir);
	}

	return res;
}

static char du_buf[S3K_MAX_PATH_LEN];

err_t disk_usage(char *path, uint32_t *ret)
{
	uint32_t sum = 0;
	ssize_t r = strscpy(du_buf, path, sizeof(du_buf));
	if (r < 0)
		return ERR_PATH_TOO_LONG;
	FRESULT fr = f_disk_usage(du_buf, &sum);
	if (fr == FR_OK) {
		*ret = sum;
		return SUCCESS;
	}
	alt_printf("FF error: %s\n", fresult_get_error(fr));
	if (fr == FR_NOT_ENOUGH_CORE) {
		return ERR_PATH_TOO_LONG;
	}
	return ERR_PATH_STAT;
}

err_t create_dir(cte_t c_path, bool ensure_create)
{
	cap_t path = cte_cap(c_path);
	if (path.path.type != CAPTY_PATH || path.path.file || !path.path.write)
		return ERR_INVALID_INDEX;

	FRESULT fr;
	FILINFO fno;
	fr = f_stat(nodes[path.path.tag].path, &fno);
	if (fr == FR_OK) {
		if (ensure_create)
			return ERR_PATH_EXISTS;
		if (!(fno.fattrib & AM_DIR))
			return ERR_PATH_EXISTS;
		return SUCCESS;
	} else if (fr != FR_NO_FILE) {
		alt_printf("FF error: %s\n", fresult_get_error(fr));
		alt_printf("\tfrom running stat on: %s\n", nodes[path.path.tag].path);
		return ERR_PATH_STAT;
	}
	const uint32_t growing_by = METADATA_DIR_ENTRY_SIZE;
	if (path.path.create_quota < growing_by) {
		return ERR_FILE_SIZE;
	}

	fr = f_mkdir(nodes[path.path.tag].path);
	if (fr != FR_OK) {
		alt_printf("FF error: %s\n", fresult_get_error(fr));
		alt_printf("\tfrom running f_mkdir on: %s\n", nodes[path.path.tag].path);
		return ERR_FILE_WRITE;
	}

	path.path.create_quota -= growing_by;
	cte_set_cap(c_path, path);

	return SUCCESS;
}

err_t write_file(cte_t c_path, uint32_t offset, uint8_t *buf, uint32_t buf_size,
		 uint32_t *bytes_written)
{
	cap_t path = cte_cap(c_path);
	if (path.path.type != CAPTY_PATH || !path.path.file || !path.path.write)
		return ERR_INVALID_INDEX;

	FIL Fil; /* File object needed for each open file */
	FRESULT fr;
	err_t err = SUCCESS;

	FILINFO fi;
	uint32_t growing_by = 0;
	fr = f_stat(nodes[path.path.tag].path, &fi);
	if (!(fr == FR_OK || fr == FR_NO_FILE)) {
		err = ERR_PATH_STAT;
		goto ret;
	}
	bool exists = fr == FR_OK;
	if (fr == FR_NO_FILE) {
		growing_by = METADATA_DIR_ENTRY_SIZE + offset + buf_size;
	} else if (exists && offset + buf_size > fi.fsize) {
		growing_by = offset + buf_size - fi.fsize;
	}
	// Need to check create quota is enough
	if (growing_by > path.path.create_quota) {
		err = ERR_FILE_SIZE;
		goto ret;
	}
	// FA_OPEN_ALWAYS means open the existing file or create it, i.e. succeed in both cases
	fr = f_open(&Fil, nodes[path.path.tag].path, FA_WRITE | FA_OPEN_ALWAYS);
	if (fr != FR_OK) {
		alt_printf("FF error: %s\n", fresult_get_error(fr));
		err = ERR_FILE_OPEN;
		goto ret;
	}

	fr = f_lseek(&Fil, offset);
	if (fr != FR_OK) {
		alt_printf("FF error: %s\n", fresult_get_error(fr));
		err = ERR_FILE_SEEK;
		goto cleanup;
	}
	fr = f_write(&Fil, buf, buf_size, bytes_written);
	if (fr != FR_OK) {
		alt_printf("FF error: %s\n", fresult_get_error(fr));
		err = ERR_FILE_WRITE;
		goto cleanup;
	}

	if (growing_by) {
		path.path.create_quota -= growing_by;
		cte_set_cap(c_path, path);
	}

cleanup:
	f_close(&Fil);
ret:
	return err;
}

uint32_t path_parent_tag(cap_t cap)
{
	return nodes[cap.path.tag].parent;
}

void cap_path_clear(cap_t cap)
{
	// Set to not occupied, remove all referencese in tree.
	/*
			A
		   / \
		  B   C
	    / | \
	   D  E  F

	   Clearing path B needs to update A to have first_child = D,
	   setting D, E, and F's parent to A, and setting F's next_sibling
	   equal to C (what B already had there).
	*/
	uint32_t del_idx = cap.path.tag;
	tree_node_t *del_node = &nodes[del_idx];
	tree_node_t *del_parent_node = &nodes[del_node->parent];

	// Find the location referencing "B", if like above example, this is
	// "A" directly, but otherwise we iterate siblings until we find it.
	uint32_t *ref_to_del_node = &del_parent_node->first_child;
	while (*ref_to_del_node != del_idx)
		ref_to_del_node = &nodes[*ref_to_del_node].next_sibling;

	// The reference should reference the first child if the del_node has children,
	// or the next sibling if not.
	*ref_to_del_node = del_node->first_child ? del_node->first_child : del_node->next_sibling;

	if (del_node->first_child) {
		// Update all del_node children to have correct new parent
		tree_node_t *del_node_child = &nodes[del_node->first_child];
		while (del_node_child->next_sibling) { // While we are not the last child/sibling
			del_node_child->parent = del_node->parent;
			del_node_child = &nodes[del_node_child->next_sibling];
		}
		// And for the last child (last sibling), also set it's next sibling to what "B"
		// had as a sibling.
		del_node_child->parent = del_node->parent;
		del_node_child->next_sibling = del_node->next_sibling;
	}

	// Clear the memory (i.e. occupied = false etc)
	memset(del_node, 0, sizeof(tree_node_t));
}

err_t path_delete(cte_t c_path)
{
	cap_t path = cte_cap(c_path);
	if (path.path.type != CAPTY_PATH || !path.path.write)
		return ERR_INVALID_INDEX;

	FILINFO fi;
	FRESULT fr = f_stat(nodes[path.path.tag].path, &fi);
	if (fr != FR_OK) {
		return ERR_PATH_STAT;
	}
	uint32_t truncation_size
	    = (fi.fsize + METADATA_DIR_ENTRY_SIZE); // Relies on fsize being 0 for directories
	fr = f_unlink(nodes[path.path.tag].path);
	if (fr == FR_DENIED) {
		// Not empty, is current directory, or read-only attribute
		return ERR_PATH_EXISTS;
	} else if (fr != FR_OK) {
		alt_printf("FF error: %s\n", fresult_get_error(fr));
		return ERR_FILE_WRITE;
	}
	path.path.create_quota += truncation_size;
	cte_set_cap(c_path, path);

	return SUCCESS;
}