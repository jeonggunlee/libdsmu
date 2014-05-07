#ifndef _RPC_H_
#define _RPC_H_

int sendman(char *str, int len);

int initsocks(int port, volatile int waitingap[MAX_SHARED_PAGES]);

int teardownsocks(void);

void *listenman(void *ptr);

void confirminvalidate(int pgnum);

void confirminvalidate_encoded(int pgnum, char *pgb64);

int invalidate(char *msg);

int dispatch(char *msg);

int requestpage(int pgnum, char *type);

int handleconfirm(char *msg);

#endif  // _RPC_H_
