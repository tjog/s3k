#include "altc/string.h"
#include "cap_ops.h"
#include "cap_table.h"
#include "cap_util.h"
#include "error.h"
#include "ff.h"
#include "proc.h"

#define MAX_PATH 100
#define MAX_NODES 100

// In addition to the cap_table we need to reliably track parent relationships,
// otherwise revocation could touch unrelated capabilities due to how the CDT works
// (CTE next, prev pointers only)
typedef struct {
	uint32_t parent;
	uint32_t next_sibling;
	uint32_t first_child;
	char path[MAX_PATH];
	bool occupied;
} tree_node_t;

// Each node is identified by its tag, the offset on the nodes array.
static uint32_t current_idx = 0;
static tree_node_t nodes[MAX_NODES] = {
    [0] = {
	   .parent = 0,
	   .first_child = 0,
	   .next_sibling = 0,
	   .path = "/",
	   .occupied = true,
	   }
};
_Static_assert(sizeof(nodes) <= (1 << 14)); /* Not more than 16 KiB */

FATFS FatFs; /* FatFs work area needed for each volume */

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

void fs_init()
{
	FRESULT fr;
	fr = f_mount(&FatFs, "", 0); /* Give a work area to the default drive */
	if (fr == FR_OK) {
		alt_puts("File system mounted OK");
	} else {
		alt_printf("File system not mounted: %s\n", fresult_get_error(fr));
	}
}

uint32_t find_next_free_idx()
{
	uint32_t idx = current_idx;
	while (nodes[idx].occupied) {
		idx = (idx + 1) % MAX_NODES;
		// If we have gone a full cycle without finding a free tag
		if (idx == current_idx)
			return -1;
	}
	return idx;
}

#include <stdint.h>

// The RISC-V GNU toolchain does not seem to define ssize_t
typedef long ssize_t;
_Static_assert(sizeof(ssize_t) == sizeof(size_t));

/**
 * strlen - Find the length of a string
  */
size_t strlen(const char *s)
{
	const char *sc;
	for (sc = s; *sc != '\0'; ++sc)
		/* nothing */;
	return sc - s;
}

/**
 * strscpy - Copy a C-string into a sized buffer
 */
ssize_t strscpy(char *dest, const char *src, size_t count)
{
	long res = 0;

	if (count == 0)
		return -1; // Too small

	while (count) {
		char c;

		c = src[res];
		dest[res] = c;
		if (!c)
			return res;
		res++;
		count--;
	}

	/* Hit buffer length without finding a NUL; force NUL-termination. */
	if (res)
		dest[res - 1] = '\0';

	return -1; // Too big
}

/**
 * strlcat - Append a length-limited, C-string to another
 * The strlcat() function appends the NUL-terminated
 * string src to the end of dst. It will append at most
 * size - strlen(dst) - 1 bytes, NUL-terminating the result.
 * 
 * This is not the "pure" version, as an error is returned
 * if the src string would not fit
 */
ssize_t strlcat(char *dest, const char *src, size_t count)
{
	size_t dsize = strlen(dest);
	size_t len = strlen(src);
	size_t res = dsize + len;

	if (res >= count)
		return -1; // Too big (equals because we need null terminator)

	dest += dsize;
	count -= dsize;
	if (len >= count)
		len = count - 1;
	memcpy(dest, src, len);
	dest[len] = 0;
	return res;
}

err_t path_read(cap_t path, char *buf, size_t n)
{
	ssize_t ret = strscpy(buf, nodes[path.path.tag].path, n);
	if (ret < 0)
		return ERR_PATH_TOO_LONG;
	return SUCCESS;
}

err_t path_derive(cte_t src, cte_t dst, const char *path, path_flags_t flags)
{
	if (!cte_cap(src).type)
		return ERR_SRC_EMPTY;

	if (cte_cap(dst).type)
		return ERR_DST_OCCUPIED;
	cap_t scap = cte_cap(src);

	if (scap.type != CAPTY_PATH)
		return ERR_INVALID_DERIVATION;

	// Can only derive file to file when not using a new path i.e NULL pointer (0)
	if (scap.path.file && path)
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
	    .next_sibling = nodes[src_node->first_child].next_sibling,
	    // Path is strscpy'd below after
	    // Occupied only updated after we are sure to finish the derivation
	};

	src_node->first_child = new_idx;
	// Copy path
	strscpy(nodes[new_idx].path, src_node->path, MAX_PATH);
	if (path) {
		// Append path separator and new path
		ssize_t ret = strlcat(nodes[new_idx].path, "/", MAX_PATH);
		if (ret < 0)
			return ERR_PATH_TOO_LONG;
		ret = strlcat(nodes[new_idx].path, path, MAX_PATH);
		if (ret < 0)
			return ERR_PATH_TOO_LONG;
	}
	cap_t ncap = cap_mk_path(new_idx, flags);
	cte_insert(dst, ncap, src);
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

err_t path_revoke(cte_t src, cte_t dst, const char *path, path_flags_t flags)
{
	if (!cte_cap(src).type)
		return ERR_SRC_EMPTY;

	if (cte_cap(dst).type)
		return ERR_DST_OCCUPIED;
	cap_t scap = cte_cap(src);

	if (scap.type != CAPTY_PATH)
		return ERR_INVALID_DERIVATION;

	// Can only derive file to file when not using a new path i.e NULL pointer (0)
	if (scap.path.file && path != NULL)
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
	    .next_sibling = nodes[src_node->first_child].next_sibling,
	    // Path is strscpy'd below after
	    .occupied = true};

	src_node->first_child = new_idx;
	strscpy(nodes[new_idx].path, path, MAX_PATH);
	cap_t ncap = cap_mk_path(new_idx, flags);

	current_idx = new_idx;

	cte_insert(dst, ncap, src);
	cte_set_cap(src, scap);

	return SUCCESS;
}

err_t read_file(cap_t path, uint32_t offset, uint8_t *buf, uint32_t buf_size, uint32_t *bytes_read)
{
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
	}
	return SUCCESS;
}

err_t write_file(cap_t path, uint32_t offset, uint8_t *buf, uint32_t buf_size,
		 uint32_t *bytes_written)
{
	FIL Fil; /* File object needed for each open file */
	FRESULT fr;
	err_t err = SUCCESS;

	fr = f_open(&Fil, nodes[path.path.tag].path, FA_WRITE | FA_CREATE_ALWAYS);
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
		err = ERR_FILE_READ;
		goto cleanup;
	}
cleanup:
	f_close(&Fil);
ret:
	return err;
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

	*ref_to_del_node = del_node->first_child;

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

	// Clear the memory
	memset(del_node, 0, sizeof(tree_node_t));
}
