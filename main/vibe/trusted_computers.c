#include "trusted_computers.h"

static int find(const vibe_trusted_computers_t *s,const char *client) {
    if (!client || s->count>VIBE_TRUSTED_MAX) return -1;
    for (unsigned i=0;i<s->count;i++) if (!strcmp(s->entries[i].client,client)) return (int)i;
    return -1;
}
bool vibe_trusted_matches(const vibe_trusted_computers_t *s,const char *client,const char *token) {
    int i=find(s,client);
    return i>=0 && token && !strcmp(s->entries[i].token,token);
}
bool vibe_trusted_has(const vibe_trusted_computers_t *s,const char *client) { return find(s,client)>=0; }
bool vibe_trusted_put(vibe_trusted_computers_t *s,const char *client,const char *token) {
    if (!vibe_connect_identity(client) || !vibe_connect_digest(token) || s->count>VIBE_TRUSTED_MAX) return false;
    int index=find(s,client);
    for (unsigned i=0;i<s->count;i++) if ((int)i!=index && !strcmp(s->entries[i].token,token)) return false;
    if (index<0) {
        if (s->count==VIBE_TRUSTED_MAX) return false;
        index=s->count++;
    }
    strcpy(s->entries[index].client,client);strcpy(s->entries[index].token,token);return true;
}
bool vibe_trusted_remove(vibe_trusted_computers_t *s,const char *client) {
    int i=find(s,client);if (i<0) return false;
    memmove(&s->entries[i],&s->entries[i+1],(s->count-(unsigned)i-1)*sizeof(s->entries[0]));
    memset(&s->entries[--s->count],0,sizeof(s->entries[0]));return true;
}
size_t vibe_trusted_encode(const vibe_trusted_computers_t *s,void *bytes,size_t capacity) {
    if (s->count>VIBE_TRUSTED_MAX || capacity<5+s->count*100u) return 0;
    uint8_t *out=bytes;memcpy(out,"VTC1",4);out[4]=s->count;
    for (unsigned i=0;i<s->count;i++) {
        if (!vibe_connect_identity(s->entries[i].client) || !vibe_connect_digest(s->entries[i].token)) return 0;
        for (unsigned j=0;j<i;j++) if (!strcmp(s->entries[j].client,s->entries[i].client) || !strcmp(s->entries[j].token,s->entries[i].token)) return 0;
        memcpy(out+5+i*100,s->entries[i].client,36);memcpy(out+41+i*100,s->entries[i].token,64);
    }
    return 5+s->count*100u;
}
bool vibe_trusted_decode(vibe_trusted_computers_t *s,const void *bytes,size_t length) {
    const uint8_t *in=bytes;
    if (!in || length<5 || memcmp(in,"VTC1",4) || in[4]>VIBE_TRUSTED_MAX || length!=5+in[4]*100u) return false;
    vibe_trusted_computers_t next={0};
    for (unsigned i=0;i<in[4];i++) {
        char client[37]={0},token[65]={0};memcpy(client,in+5+i*100,36);memcpy(token,in+41+i*100,64);
        /* Duplicate identities must not silently replace an earlier row. */
        if (find(&next,client)>=0 || !vibe_trusted_put(&next,client,token)) return false;
    }
    *s=next;return true;
}
