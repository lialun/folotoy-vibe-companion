#include "radio_source.h"
#include "radio_hls.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <new>
#include <strings.h>

namespace { constexpr char kTag[] = "radio_hls"; }
struct RadioSource::Redirect {
    char *resolved;
    char target[radio_hls::kUrl]{};
    bool present{}, valid{};
};
struct RadioSource::Hls {
    radio_hls::Playlist list;
    char url[radio_hls::kUrl]{};
    char line[1024]{};
    size_t index{};
    uint64_t next{};
    int64_t advanced_at{};
};
RadioSource::~RadioSource() {
    close_http();
    if (hls_ && workspace_) hls_->~Hls();
    else delete hls_;
}
void RadioSource::close_http() {
    if (client_) { esp_http_client_close(client_); esp_http_client_cleanup(client_); client_ = nullptr; }
}
esp_err_t RadioSource::event(esp_http_client_event_t *event) {
    if (event->event_id != HTTP_EVENT_ON_HEADER || !event->user_data || !event->header_key || !event->header_value) return ESP_OK;
    auto &self = *static_cast<RadioSource *>(event->user_data);
    if (!strcasecmp(event->header_key, "Location") && self.redirect_ && self.redirect_->resolved) {
        auto &redirect = *self.redirect_;
        redirect.present = true;
        redirect.valid = radio_hls::resolve(redirect.resolved, event->header_value,
                                           redirect.target, sizeof(redirect.target));
    }
    if (!strcasecmp(event->header_key, "Content-Type")) std::snprintf(self.mime_, sizeof(self.mime_), "%s", event->header_value);
    if (!strcasecmp(event->header_key, "icy-metaint")) {
        const unsigned long n = std::strtoul(event->header_value, nullptr, 10);
        if (n <= 16 * 1024 * 1024) self.icy_interval_ = n;
    }
    return ESP_OK;
}
bool RadioSource::request(const char *url, char *resolved) {
    close_http();
    if (cancelled() || (!radio_hls::starts(url, "http://") && !radio_hls::starts(url, "https://"))) return false;
    if (resolved) {
        if (std::strlen(url) >= radio_hls::kUrl) return false;
        if (resolved != url) std::strcpy(resolved, url);
    }
    // IDF 5.5.3's get_url() omits the query string. Track the complete URL from
    // our input and Location headers instead; CDN sessions need it on refresh.
    // Scratch space lives on the existing worker stack, never in a resident heap buffer.
    Redirect redirect{resolved};
    redirect_ = &redirect;
    struct HeaderScope { Redirect *&slot; ~HeaderScope() { slot = nullptr; } } scope{redirect_};
    esp_http_client_config_t cfg{};
    cfg.url = url; cfg.timeout_ms = 6000; cfg.buffer_size = 1024; cfg.buffer_size_tx = 512;
    cfg.user_agent = "FoloToy-Vibe-Companion/0.1.0";
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    // A live TLS stream repeatedly needs a full receive record. Reserve that
    // block after the handshake, before the codec/FIFO fragment the heap. TX
    // remains dynamic; closing this connection releases the RX block as well.
    cfg.tls_dyn_buf_strategy = HTTP_TLS_DYN_BUF_RX_STATIC;
    cfg.event_handler = event; cfg.user_data = this; cfg.keep_alive_enable = true;
    client_ = esp_http_client_init(&cfg);
    if (!client_) { oom_ = true; return false; }
    esp_http_client_set_header(client_, "Accept-Encoding", "identity");
    esp_http_client_set_header(client_, "Icy-MetaData", "0");
    for (unsigned redirects = 0; redirects <= 4 && !cancelled(); ++redirects) {
        mime_[0] = 0; icy_interval_ = 0;
        redirect.present = redirect.valid = false;
        const int opened=esp_http_client_open(client_,0);
        if (opened!=ESP_OK) { ESP_LOGW(kTag,"Connection open failed: %d",opened);return false; }
        if (cancelled()) return false;
        const auto headers=esp_http_client_fetch_headers(client_);
        if (headers<0) { ESP_LOGW(kTag,"Response headers failed: %ld",(long)headers);return false; }
        if (cancelled()) return false;
        const int status = esp_http_client_get_status_code(client_);
        if (status >= 200 && status < 300) { esp_http_client_set_timeout_ms(client_,200);return true; }
        if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308) {
            ESP_LOGW(kTag, "HTTP status %d", status); return false;
        }
        if (resolved && (!redirect.present || !redirect.valid)) {
            ESP_LOGW(kTag, "Missing, invalid or oversized redirect target"); return false;
        }
        // Give IDF an absolute destination: its set_redirection()/set_url()
        // path does not resolve relative Location values against the old path.
        if (esp_http_client_set_url(client_, redirect.target) != ESP_OK) return false;
        if (resolved) std::strcpy(resolved, redirect.target);
        esp_http_client_close(client_);
    }
    return false;
}
bool RadioSource::playlist(bool initial, bool already_open) {
    for (unsigned depth = 0; depth < 3 && !cancelled(); ++depth) {
        if (!(depth == 0 && already_open) && !request(hls_->url, hls_->url)) return false;
        hls_->list.reset(hls_->next, initial);
        size_t used = 0, total = 0;
        uint8_t chunk[256];
        bool done = false;
        const int64_t deadline = esp_timer_get_time() + 10000000;
        while (!cancelled() && esp_timer_get_time() < deadline) {
            const int n = esp_http_client_read(client_, reinterpret_cast<char *>(chunk), sizeof(chunk));
            if (n == -ESP_ERR_HTTP_EAGAIN) continue;
            if (n < 0) return false;
            if (!n) { done = esp_http_client_is_complete_data_received(client_); break; }
            total += n;
            if (total > 48 * 1024) return false;
            for (int i = 0; i < n; ++i) {
                if (chunk[i] == '\n') {
                    hls_->line[used] = 0; hls_->list.line(hls_->line, hls_->url); used = 0;
                    if (hls_->list.failed) break;
                } else {
                    if (!chunk[i] || used + 1 >= sizeof(hls_->line)) return false;
                    hls_->line[used++] = char(chunk[i]);
                }
            }
            if (hls_->list.failed) break;
        }
        if (used) { hls_->line[used] = 0; hls_->list.line(hls_->line, hls_->url); }
        close_http();
        if (!done || !hls_->list.complete()) {
            ESP_LOGW(kTag, "HLS playlist rejected: initial=%d bytes=%u complete=%d valid=%d failed=%d master=%d media=%d segments=%u",
                int(initial), unsigned(total), int(done), int(hls_->list.valid), int(hls_->list.failed),
                int(hls_->list.master), int(hls_->list.media), unsigned(hls_->list.count)); return false;
        }
        if (hls_->list.master) {
            std::snprintf(hls_->url, sizeof(hls_->url), "%s", hls_->list.variant);
            continue;
        }
        hls_->index = initial && !hls_->list.ended && hls_->list.count > 3 ? hls_->list.count - 3 : 0;
        return true;
    }
    return false;
}
bool RadioSource::next_segment() {
    while (!cancelled()) {
        if (hls_->index < hls_->list.count) {
            const auto &segment = hls_->list.segments[hls_->index++];
            // A discontinuity needs a fresh demux/decoder, never concatenate
            // unrelated timestamp/configuration epochs into a live decoder.
            if (hls_->next && (segment.discontinuity || segment.sequence != hls_->next)) {
                ESP_LOGW(kTag, "HLS discontinuity/window gap: reconnecting decoder"); return false;
            }
            // Playlist line scratch is unused while downloading a segment.
            if (!request(segment.url, hls_->line)) return false;
            hls_->next = segment.sequence + 1;
            hls_->advanced_at = esp_timer_get_time();
            return true;
        }
        if (hls_->list.ended) { finished_ = true; return false; }
        if (esp_timer_get_time() - hls_->advanced_at > 90000000) return false;
        if (!playlist(false)) return false;
        // Refresh immediately when the queue drains. Only wait if the live
        // server has not published the next segment yet.
        if (!hls_->list.count && !hls_->list.ended) {
            for (unsigned i = 0; i < hls_->list.target * 5 && !cancelled(); ++i) vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    return false;
}
bool RadioSource::open(const char *url) {
    char resolved[radio_hls::kUrl];
    if (!request(url, resolved)) return false;
    if (std::strstr(url, ".m3u8") || strcasestr(mime_, "mpegurl")) {
        static_assert(sizeof(Hls) <= kHlsWorkspaceBytes);
        if (workspace_) {
            if (workspace_bytes_ < sizeof(Hls) || reinterpret_cast<uintptr_t>(workspace_) % alignof(Hls)) return false;
            hls_ = new (workspace_) Hls;
        } else hls_ = new (std::nothrow) Hls;
        if (!hls_) { oom_ = true; return false; }
        std::strcpy(hls_->url, resolved);
        if (!playlist(true, true) || !next_segment()) return false;
    }
    // Inspect actual bytes, not the directory's often incorrect codec label.
    unsigned timeouts=0;
    while (prefix_size_ < sizeof(prefix_) && !cancelled()) {
        const int n = esp_http_client_read(client_, reinterpret_cast<char *>(prefix_ + prefix_size_), sizeof(prefix_) - prefix_size_);
        if (n == -ESP_ERR_HTTP_EAGAIN) { if (++timeouts >= 25) break;continue; }
        if (n <= 0) break;
        timeouts=0;prefix_size_ += n;
    }
    if (prefix_size_ < 4) return false;
    if (prefix_size_ >= 377 && prefix_[0] == 0x47 && prefix_[188] == 0x47 && prefix_[376] == 0x47) type_ = ESP_AUDIO_SIMPLE_DEC_TYPE_TS;
    else if ((prefix_[0] == 0xff && (prefix_[1] & 0xf6) == 0xf0) || strcasestr(mime_, "aac")) type_ = ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
    else if (prefix_[0] == 0xff || std::memcmp(prefix_, "ID3", 3) == 0) {
        // Packed AAC can begin with ID3. HLS defaults to AAC in that case;
        // direct MP3 streams keep their existing ID3 path.
        type_ = hls_ && prefix_[0] == 'I' ? ESP_AUDIO_SIMPLE_DEC_TYPE_AAC : ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    } else if (!hls_ && strcasestr(mime_, "audio/mpeg")) {
        // Icecast can attach mid-frame. Let the MP3 parser find the next sync
        // rather than rejecting a working stream because byte zero is payload.
        type_ = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    } else { ESP_LOGW(kTag, "Unsupported media bytes (%s)", mime_); return false; }
    ESP_LOGI(kTag, "Source transport=%s decoder=%d", hls_ ? "HLS" : "direct", int(type_));
    return true;
}
int RadioSource::read(uint8_t *data, size_t size) {
    if (cancelled()) return -1;
    if (prefix_pos_ < prefix_size_) {
        const size_t n = std::min(size, prefix_size_ - prefix_pos_);
        std::memcpy(data, prefix_ + prefix_pos_, n); prefix_pos_ += n; return int(n);
    }
    unsigned timeouts=0;
    while (!cancelled()) {
        const int n = esp_http_client_read(client_, reinterpret_cast<char *>(data), size);
        if (n == -ESP_ERR_HTTP_EAGAIN && ++timeouts<25) continue;
        if (n<0) ESP_LOGW(kTag,"Body read failed: result=%d timeouts=%u",n,timeouts);
        if (n==0 && !esp_http_client_is_complete_data_received(client_)) ESP_LOGW(kTag,"Body ended before completion");
        if (n != 0) return n;
        if (!esp_http_client_is_complete_data_received(client_)) return -1;
        if (!hls_) { finished_ = true; return 0; }
        if (!next_segment()) return finished_ ? 0 : -1;
    }
    return -1;
}
