#pragma once
#include "radio_player.h"

// Shared presets: one catalog failure must not leave the radio with one station.
inline constexpr RadioStation kRadioPresets[] = {
#ifdef PASSPORT_RADIO_HLS_TEST
    {"经济之声", "HLS测试 · TS/AAC", "http://ngcdn002.cnr.cn/live/jjzs/index.m3u8", 0},
    {"华语金曲", "HLS测试 · AAC分片", "http://ls.qingting.fm/live/3412131.m3u8?bitrate=64", 0},
    {"凤凰资讯", "HLS测试 · TS/AAC", "http://playtv-live.ifeng.com/live/06OLEEWQKN4_audio.m3u8", 0},
    {"SomaFM", "美国 · MP3测试", "https://ice6.somafm.com/groovesalad-128-mp3", 0},
    {"MANGORADIO", "德国 · MP3测试", "https://mangoradio.stream.laut.fm/mangoradio", 0},
    {"中国之声", "MP3回归测试", "http://lhttp.qingting.fm/live/15318317/64k.mp3", 0},
#else
    {"CNR-1 中国之声", "推荐电台", "https://lhttp.qtfm.cn/live/15318317/64k.mp3", 0},
    {"CNR-2 经济之声", "推荐电台", "http://ngcdn002.cnr.cn/live/jjzs/index.m3u8", 0},
    {"CNR-9 文艺之声", "推荐电台", "http://satellitepull.cnr.cn/live/wxwyzs/playlist.m3u8", 0},
    {"新闻联播", "推荐电台", "https://rfienchinois64k.ice.infomaniak.ch//rfienchinois-64.mp3", 0},
    {"CCTV-10科教伴音", "推荐电台", "https://piccpndali.v.myalicdn.com/audio/cctv10_2.m3u8", 0},
    {"第一财经", "推荐电台", "http://lhttp.qingting.fm/live/276/64k.mp3", 0},
    {"国际新闻", "推荐电台", "https://lhttp.qtfm.cn/live/20500172/64k.mp3", 0},
    {"CRI环球资迅广播", "推荐电台", "https://sk.cri.cn/905.m3u8", 0},
    {"500首华语经典", "推荐电台", "https://lhttp.qtfm.cn/live/5022308/64k.mp3", 0},
    {"SPH 963 Radio", "推荐电台", "http://playerservices.streamtheworld.com/api/livestream-redirect/HAO_963AAC.aac", 0},
    {"清晨音乐台", "推荐电台", "http://lhttp.qingting.fm/live/4915/64k.mp3", 0},
    {"90.7mixfm", "推荐电台", "https://lhttp.qingting.fm/live/15318146/64k.mp3", 0},
    {"德云社相声合集", "推荐电台", "https://stream.zeno.fm/yqawwmweq8mtv", 0},
    {"AsiaFM 亚洲粤语台", "推荐电台", "https://lhttp.qtfm.cn/live/15318569/64k.mp3", 0},
    {"香港普通话台", "推荐电台", "https://rthkradiopth-live.akamaized.net/hls/live/2040082/radiopth/master.m3u8", 0},
#endif
};
