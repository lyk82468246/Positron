#ifndef PCORE_CSS_VALUES_H
#define PCORE_CSS_VALUES_H

/* Returns an allocated, NUL-terminated replacement only when at least one
 * supported modern value was converted. The caller owns the result. */
char *pcore_css_compat_values(const char *css, unsigned int len,
        unsigned int *out_len);

#endif
