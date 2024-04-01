#pragma once

#include "error.h"

err_t path_read(cap_t path, char *buf, size_t n);
err_t path_derive(cte_t src, cte_t dst, const char *path, path_flags_t flags);
err_t read_file(cap_t path, uint32_t offset, uint8_t *buf, uint32_t buf_size, uint32_t *bytes_read);
err_t write_file(cap_t path, uint32_t offset, uint8_t *buf, uint32_t buf_size,
		 uint32_t *bytes_written);
void cap_path_clear(cap_t cap);
err_t create_dir(cap_t path, bool ensure_create);
err_t path_delete(cap_t path);
