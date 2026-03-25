#define SYSC(v,c,m) \
if((v=c)==-1){perror(m);exit(errno);}

#define SYSCN(v,c,m) \
if((v=c)==NULL){perror(m);exit(errno);}
