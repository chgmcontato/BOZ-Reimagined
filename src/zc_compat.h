/* What the cod-boz-port ZeroConf sources expect from their host header. */
#ifndef CODBOZ_PC_ZC_COMPAT_H
#define CODBOZ_PC_ZC_COMPAT_H

#include "host.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h> /* getpid */
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#endif
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* guest function address */
typedef uint32_t s3e_zeroconf_callback_fn;

#endif
