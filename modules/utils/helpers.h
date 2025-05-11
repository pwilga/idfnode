#ifndef HELPERS_H
#define HELPERS_H

/**
 * @brief Returns a sanitized lowercase copy of the input string.
 *
 * Spaces are replaced with underscores, uppercase letters are lowercased.
 *
 * @param s Input null-terminated C string.
 * @return char* Newly allocated sanitized string. Must be freed by caller.
 */
char *sanitize(const char *s);

#endif // HELPERS_H