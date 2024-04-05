#pragma once

#include "error.h"

err_t path_read(cap_t path, char *buf, size_t n);
err_t path_derive(cte_t src, cte_t dst, const char *path, path_flags_t flags);
void cap_path_clear(cap_t cap);
