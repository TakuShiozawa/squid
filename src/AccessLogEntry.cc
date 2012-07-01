#include "squid.h"
#include "AccessLogEntry.h"
#include "HttpRequest.h"
#include "ssl/support.h"

#if USE_SSL
AccessLogEntry::Ssl::Ssl(): user(NULL), bumpMode(::Ssl::bumpEnd)
{
}
#endif /* USE_SSL */


void
AccessLogEntry::getLogClientIp(char *buf, size_t bufsz) const
{
#if FOLLOW_X_FORWARDED_FOR
    if (Config.onoff.log_uses_indirect_client && request)
        request->indirect_client_addr.NtoA(buf, bufsz);
    else
#endif
        if (tcpClient != NULL)
            tcpClient->remote.NtoA(buf, bufsz);
        else if (cache.caddr.IsNoAddr()) // e.g., ICAP OPTIONS lack client
            strncpy(buf, "-", 1);
        else
            cache.caddr.NtoA(buf, bufsz);
}
