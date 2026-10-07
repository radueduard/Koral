/*
 * koral-net: what crosses out of the module's library. C as well as C++: koralNet_c.h includes it.
 */

#ifndef KNET_EXPORT_H
#define KNET_EXPORT_H

/*
 * Koral and its modules build with hidden visibility, so what a consumer calls has to say so. Exported here,
 * imported everywhere else; the module's own build defines KORAL_NET_EXPORTS.
 */
#if defined(_WIN32)
#  if defined(KORAL_NET_EXPORTS)
#    define KNET_API __declspec(dllexport)
#  else
#    define KNET_API __declspec(dllimport)
#  endif
#else
#  define KNET_API __attribute__((visibility("default")))
#endif

#endif
