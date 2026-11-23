#ifndef WEB_BRIDGE_H
#define WEB_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

int  web_bridge_init(int http_port, int mud_port);
void web_bridge_poll(void);
void web_bridge_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
