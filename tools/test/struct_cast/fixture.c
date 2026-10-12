/* tools/struct_cast_audit.py fixture: each cast marked with an expect comment
   must be reported, nothing else may be. */
typedef struct Inner {
    void *p;
    int n;
} Inner;

typedef struct Wide {
    Inner a[3]; /* 48 bytes on the host */
    float v[4];
} Wide;

static float fileBuf[2][4]; /* 32 bytes */
static char bytes[0x10];

void fixture(void)
{
    char local[32];
    Inner small;
    Wide ok;
    Wide *w;
    Inner *q;

    w = (Wide *)fileBuf; /* EXPECT fileBuf Wide */
    w = (Wide *)bytes;   /* EXPECT bytes Wide */
    w = (Wide *)(char *)local; /* EXPECT local Wide */
    w = (Wide *)&small;  /* EXPECT small Wide */
    q = (Inner *)&ok;    /* a smaller struct over a larger one is fine */
    q = (Inner *)fileBuf; /* 16 of its 32 bytes */
    q = (Inner *)&bytes[0]; /* an element address is not looked at */
}
