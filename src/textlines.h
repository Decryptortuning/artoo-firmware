#ifndef TEXTLINES_H
#define TEXTLINES_H
#include <stddef.h>
namespace TextLines {
template<typename Emit>
void visit(const char *text, Emit emit) {
    const char *begin = text;
    for (const char *cursor = text;; ++cursor) {
        if (*cursor == '\n' || *cursor == '\0') {
            emit(begin, size_t(cursor - begin));
            if (*cursor == '\0') return;
            begin = cursor + 1;
        }
    }
}
}
#endif
