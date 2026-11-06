#ifndef ABOUT_H
#define ABOUT_H
#include <intuition/intuition.h>
/* Modal About window; returns when the user closes it. */
void about_show(struct Window *parent, struct TextAttr *ta);
#endif
