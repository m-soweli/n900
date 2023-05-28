void screeninit(void);
void flushmemscreen(Rectangle);
Memdata *attachscreen(Rectangle *, ulong *, int *, int *, int *);

#define ishwimage(i) 1

void mousectl(Cmdbuf*);
void mouseresize(void);
void mouseredraw(void);
