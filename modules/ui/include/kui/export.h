/*
 * koral-ui: what crosses out of the module's library. C as well as C++: koralUI_c.h includes it.
 */

#ifndef KUI_EXPORT_H
#define KUI_EXPORT_H

/*
 * Marks what crosses out of the module's library.
 *
 * Koral and its modules build with hidden visibility, so a class a consumer calls has to say so.
 * Exported here, imported everywhere else; the module's own build defines KORAL_UI_EXPORTS.
 */
#if defined(_WIN32)
#  if defined(KORAL_UI_EXPORTS)
#    define KUI_API __declspec(dllexport)
#  else
#    define KUI_API __declspec(dllimport)
#  endif
#else
#  define KUI_API __attribute__((visibility("default")))
#endif

#endif
