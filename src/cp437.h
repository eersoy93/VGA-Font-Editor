/*
 * Code page 437 (the IBM PC / VGA character set) <-> Unicode mapping.
 */
#ifndef CP437_H
#define CP437_H

/* Unicode code point shown by a VGA for each of the 256 character codes. */
extern const unsigned short cp437_to_unicode[256];

/* Returns the CP437 code for a Unicode code point, or -1 if there is none. */
int unicode_to_cp437(unsigned int code_point);

#endif
