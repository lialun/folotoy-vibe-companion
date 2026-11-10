#include "protocol.h"
/* Reject nesting before cJSON recurses on the ESP32 task's bounded stack. */
bool vibe_json_depth_ok(const char *s, size_t len) {
    int depth=0; bool quoted=false;
    for (size_t i=0;i<len;i++) {
        if (!s[i]) return false;
        if (quoted) {
            if (s[i]=='\\') { if (++i>=len) return false; }
            else if (s[i]=='"') quoted=false;
        } else if (s[i]=='"') quoted=true;
        else if (s[i]=='{' || s[i]=='[') { if (++depth>8) return false; }
        else if (s[i]=='}' || s[i]==']') { if (--depth<0) return false; }
    }
    return !quoted && depth==0;
}
