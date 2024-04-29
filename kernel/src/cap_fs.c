#include "altc/string.h"
#include "cap_ops.h"
#include "cap_table.h"
#include "cap_util.h"
#include "error.h"
#include "proc.h"

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
    [0] = {
	   .parent = 0,
	   .first_child = 0,
	   .next_sibling = 0,
	   .path = "/",
	   .occupied = true,
	   }
};
_Static_assert(sizeof(nodes) <= (1 << 14)); /* Not more than 16 KiB */

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
	alt_printf(
	    "Node %d: %s, parent=%d, occupied=%d, first_child=%d, next_sibling=%d\n",
	    tag, n->path, n->parent, n->occupied, n->first_child,
	    n->next_sibling);

	// Recursively dump children
	uint32_t child = n->first_child;
	while (child) {
		dump_tree(child, depth + 1);
		child = nodes[child].next_sibling;
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

err_t path_derive(cte_t src, cte_t dst, const char *path, path_flags_t flags)
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

	src_node->first_child = new_idx;
	// Copy path
	strscpy(nodes[new_idx].path, src_node->path, S3K_MAX_PATH_LEN);
	if (path) {
		// Append path separator and new path
		ssize_t ret
		    = strlcat(nodes[new_idx].path, "/", S3K_MAX_PATH_LEN);
		if (ret < 0)
			return ERR_PATH_TOO_LONG;
		ret = strlcat(nodes[new_idx].path, path, S3K_MAX_PATH_LEN);
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
	*ref_to_del_node = del_node->first_child ? del_node->first_child :
						   del_node->next_sibling;

	if (del_node->first_child) {
		// Update all del_node children to have correct new parent
		tree_node_t *del_node_child = &nodes[del_node->first_child];
		while (
		    del_node_child
			->next_sibling) { // While we are not the last child/sibling
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
