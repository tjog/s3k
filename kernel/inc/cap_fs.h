#pragma once

#include "cap_table.h"
#include "cap_types.h"
#include "error.h"
#include "stddef.h"

err_t path_read(cap_t path, char *buf, size_t n);
err_t path_derive(cte_t src, cte_t dst, const char *path, uint32_t create_quota,
		  path_flags_t flags);
err_t read_file(cap_t path, uint32_t offset, uint8_t *buf, uint32_t buf_size, uint32_t *bytes_read);
err_t write_file(cte_t c_path, uint32_t offset, uint8_t *buf, uint32_t buf_size,
		 uint32_t *bytes_written);
uint32_t path_parent_tag(cap_t cap);
void cap_path_clear(cap_t cap);
err_t create_dir(cte_t c_path, bool ensure_create);
err_t read_dir(cap_t path, size_t dir_entry_idx, dir_entry_info_t *out);
err_t path_delete(cte_t c_path);
