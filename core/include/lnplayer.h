#ifndef LNPLAYER_H
#define LNPLAYER_H

#if defined(_WIN32)
    #if defined(LNPLAYER_BUILD)
        #define LNPLAYER_API __declspec(dllexport)
    #else
        #define LNPLAYER_API __declspec(dllimport)
    #endif
#else
    #define LNPLAYER_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

LNPLAYER_API unsigned int lnplayer_get_abi_version(void);

#ifdef __cplusplus
}
#endif

#endif
