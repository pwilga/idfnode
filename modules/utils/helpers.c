#include <ctype.h>
#include <string.h>

char *sanitize(const char *s) {
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    if (!out)
        return NULL;

    for (size_t i = 0; i < len; i++) {
        if (s[i] == ' ')
            out[i] = '_';
        else if (isupper((unsigned char)s[i]))
            out[i] = tolower((unsigned char)s[i]);
        else
            out[i] = s[i];
    }
    out[len] = '\0';
    return out;
}
