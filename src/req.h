/* Oasis - small modal dialogs on the Workbench screen (1.3 has no EasyRequest or ASL). */
#ifndef REQ_H
#define REQ_H
#include <exec/types.h>
#include <intuition/intuition.h>

/* body: lines separated by '\n' (up to 6); buttons: "Retry|Skip|Abort" (up to 5).
   Returns the button's index. Return = first button, Esc = last, or a button's first letter. */
int  req_choose(struct Window *parent, struct TextAttr *ta, const char *title,
                const char *body, const char *buttons);

/* a one-line text field with OK / Cancel; buf holds the start text and the result */
BOOL req_string(struct Window *parent, struct TextAttr *ta, const char *title,
                const char *prompt, char *buf, int size);

/* the eight protection bits as toggles (hsparwed order, TRUE = shown on); OK / Cancel */
BOOL req_protect(struct Window *parent, struct TextAttr *ta, const char *title,
                 const char *prompt, BOOL on[8]);

#endif
