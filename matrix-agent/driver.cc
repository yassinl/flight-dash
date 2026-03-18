//driver.cc
#include "led-matrix.h"
#include "graphics.h"
#include "flight_data.h"
#include "flight_finder.h"
#include <png.h>
#include <signal.h>
#include <unistd.h>
#include <atomic>
#include <cmath>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using namespace rgb_matrix;

struct LogoImage {
    std::vector<uint8_t> pixels; // RGBA row-major
    int width = 0, height = 0;
    bool ok() const { return !pixels.empty(); }
};

static LogoImage load_png_rgba(const std::string &path) {
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) return {};
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) { fclose(fp); return {}; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, nullptr, nullptr); fclose(fp); return {}; }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr); fclose(fp); return {};
    }
    png_init_io(png, fp);
    png_read_info(png, info);
    int w = (int)png_get_image_width(png, info);
    int h = (int)png_get_image_height(png, info);
    png_byte ct = png_get_color_type(png, info);
    png_byte bd = png_get_bit_depth(png, info);
    if (bd == 16) png_set_strip_16(png);
    if (ct == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (ct == PNG_COLOR_TYPE_GRAY && bd < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (ct == PNG_COLOR_TYPE_RGB || ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
    png_read_update_info(png, info);
    LogoImage result; result.width = w; result.height = h;
    result.pixels.resize(w * h * 4);
    std::vector<png_bytep> rows(h);
    for (int y = 0; y < h; ++y) rows[y] = &result.pixels[y * w * 4];
    png_read_image(png, rows.data());
    png_destroy_read_struct(&png, &info, nullptr);
    fclose(fp);
    return result;
}

static LogoImage scale_image(const LogoImage &src, int dw, int dh) {
    LogoImage dst; dst.width = dw; dst.height = dh;
    dst.pixels.resize(dw * dh * 4);
    for (int dy = 0; dy < dh; ++dy) {
        for (int dx = 0; dx < dw; ++dx) {
            float sx = (dx + 0.5f) * src.width  / dw - 0.5f;
            float sy = (dy + 0.5f) * src.height / dh - 0.5f;
            int x0 = (int)sx, y0 = (int)sy;
            int x1 = x0 + 1, y1 = y0 + 1;
            float fx = sx - x0, fy = sy - y0;
            x0 = std::max(0, std::min(src.width -1, x0));
            x1 = std::max(0, std::min(src.width -1, x1));
            y0 = std::max(0, std::min(src.height-1, y0));
            y1 = std::max(0, std::min(src.height-1, y1));
            uint8_t *out = &dst.pixels[(dy*dw+dx)*4];
            for (int c = 0; c < 4; ++c) {
                float v = src.pixels[(y0*src.width+x0)*4+c] * (1-fx)*(1-fy)
                        + src.pixels[(y0*src.width+x1)*4+c] * fx*(1-fy)
                        + src.pixels[(y1*src.width+x0)*4+c] * (1-fx)*fy
                        + src.pixels[(y1*src.width+x1)*4+c] * fx*fy;
                out[c] = (uint8_t)(v + 0.5f);
            }
        }
    }
    return dst;
}

volatile bool interrupt_received = false;
static void InterruptHandler(int) { interrupt_received = true; }

static void refresh_loop(FlightData *fd, std::mutex *mtx, std::atomic<bool> *stop) {
    while (!*stop && !interrupt_received) {
        for (int i = 0; i < 30 && !*stop && !interrupt_received; ++i) sleep(1);
        if (*stop || interrupt_received) break;
        std::lock_guard<std::mutex> lock(*mtx);
        fd->refresh();
    }
}

static double haversine_km(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371.0;
    double dlat = (lat2-lat1)*M_PI/180, dlon = (lon2-lon1)*M_PI/180;
    double a = sin(dlat/2)*sin(dlat/2)
             + cos(lat1*M_PI/180)*cos(lat2*M_PI/180)*sin(dlon/2)*sin(dlon/2);
    return R*2.0*atan2(sqrt(a),sqrt(1.0-a));
}

static double compute_progress(const FlightState &s) {
    if (s.fa_progress_pct >= 0) return s.fa_progress_pct;
    if (!s.has_airport_pos) return -1.0;
    double total   = haversine_km(s.origin_lat,s.origin_lon,s.dest_lat,s.dest_lon);
    double covered = haversine_km(s.origin_lat,s.origin_lon,s.latitude,s.longitude);
    return total > 0 ? std::min(100.0,std::max(0.0,covered/total*100.0)) : -1.0;
}

static std::string fmt_eta(const FlightState &s) {
    long rem = 0;
    if (s.fa_eta_unix > 0) rem = (long)(s.fa_eta_unix - time(nullptr));
    else if (s.has_airport_pos && s.speed_ms > 0) {
        double total   = haversine_km(s.origin_lat,s.origin_lon,s.dest_lat,s.dest_lon);
        double covered = haversine_km(s.origin_lat,s.origin_lon,s.latitude,s.longitude);
        rem = (long)((total-covered)*1000.0/s.speed_ms);
    }
    if (rem <= 0) return "--";
    char buf[16];
    long h=rem/3600, m=(rem%3600)/60;
    if (h>0) snprintf(buf,sizeof(buf),"%ldh%02ldm",h,m);
    else      snprintf(buf,sizeof(buf),"%ldm left",m);
    return buf;
}

static void draw_bar(FrameCanvas *c, int x, int y, int w, int h,
                     double pct, const Color &fill) {
    for (int px=x; px<x+w; ++px)
        for (int py=y; py<y+h; ++py)
            c->SetPixel(px,py,40,40,40);
    int filled=(int)(pct/100.0*w+0.5);
    for (int px=x; px<x+filled; ++px)
        for (int py=y; py<y+h; ++py)
            c->SetPixel(px,py,fill.r,fill.g,fill.b);
}

static LogoImage load_logo(const std::string &callsign, int w, int h) {
    if (callsign.size() < 3) return {};
    std::string path = "../airline-logos/flightaware_logos/" + callsign.substr(0,3) + ".png";
    LogoImage raw = load_png_rgba(path);
    if (!raw.ok()) return {};
    if (raw.width == w && raw.height == h) return raw;
    return scale_image(raw, w, h);
}

static void render(FrameCanvas *canvas,
                   const Font &large, const Font &small,
                   const FlightState &s, bool landed,
                   const LogoImage &logo = {}) {
    const Color black(0,0,0), white(255,255,255);
    const bool has_logo = logo.ok();
    const int COL1 = has_logo ? 32 : 2;
    const int COL2=66, COL3=130;
    canvas->Fill(0,0,0);

    if (has_logo) {
        for (int ly = 0; ly < logo.height; ++ly)
            for (int lx = 0; lx < logo.width; ++lx) {
                const uint8_t *px = &logo.pixels[(ly*logo.width+lx)*4];
                if (px[3] > 0)
                    canvas->SetPixel(lx, ly, px[0], px[1], px[2]);
            }
    }

    int max_airline = has_logo ? 5 : 10;
    std::string airline = (int)s.airline.size() > max_airline
                          ? s.airline.substr(0, max_airline) : s.airline;
    DrawText(canvas,small,COL1, 1+small.baseline(),white,&black,airline.c_str(),0);
    DrawText(canvas,small,COL1,12+small.baseline(),white,&black,s.callsign.c_str(),0);
    DrawText(canvas,small,COL1,23+small.baseline(),white,&black,
             s.aircraft_type.empty() ? "---" : s.aircraft_type.c_str(),0);

// AFTER – origin/dest stacked below the logo on the left
const int LEFT = 2;   // x position, same left margin as COL1 text
if (!s.origin_icao.empty()) {
    DrawText(canvas,small,LEFT,32+small.baseline(),white,&black,s.origin_icao.c_str(),0);
    DrawText(canvas,small,LEFT,43+small.baseline(),white,&black,s.dest_icao.c_str(),0);

    // ETA and progress stay at COL2
    DrawText(canvas,small,COL2,23+small.baseline(),white,&black,
             landed?"LANDED":fmt_eta(s).c_str(),0);
    double pct = landed ? 100.0 : compute_progress(s);
    if (pct >= 0) {
        char pct_str[16];
        snprintf(pct_str, sizeof(pct_str), "%.0f%%", pct);
        DrawText(canvas,small,COL2,34+small.baseline(),white,&black,pct_str,0);
        draw_bar(canvas, 0, canvas->height()-6, canvas->width(), 5, pct,
                 Color(0,200,0));
    }
}

    char alt[32],spd[32],trk[32],vr[32];
    snprintf(alt,sizeof(alt),"ALT%5.0fft",s.altitude_m*3.28084f);
    snprintf(spd,sizeof(spd),"SPD%4.0fkt", s.speed_ms*1.94384f);
    snprintf(trk,sizeof(trk),"TRK%3.0fdeg",s.track_deg);
    snprintf(vr, sizeof(vr), "V/R%+.0fm/s",s.vertical_rate_ms);
    DrawText(canvas,small,COL3, 1+small.baseline(),white,&black,alt,0);
    DrawText(canvas,small,COL3,12+small.baseline(),white,&black,spd,0);
    DrawText(canvas,small,COL3,23+small.baseline(),white,&black,trk,0);
    DrawText(canvas,small,COL3,34+small.baseline(),white,&black,vr, 0);
}

int main(int argc, char *argv[]) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    const char *client_id     = getenv("CLIENT_ID");
    const char *client_secret = getenv("CLIENT_SECRET");
    const char *fa_key_env    = getenv("FLIGHTAWARE_KEY");
    if (!client_id || !client_secret || !*client_id || !*client_secret) {
        fprintf(stderr, "Missing CLIENT_ID or CLIENT_SECRET\n"); return 1;
    }
    if (!fa_key_env || !*fa_key_env)
        fprintf(stderr, "[WARN] FLIGHTAWARE_KEY not set\n");
    std::string fa_key = fa_key_env ? fa_key_env : "";

    RGBMatrix::Options opts;
    rgb_matrix::RuntimeOptions rt;
    opts.rows=32; opts.cols=64; opts.chain_length=3; opts.parallel=2; rt.gpio_slowdown=4;
    rgb_matrix::ParseOptionsFromFlags(&argc,&argv,&opts,&rt);

    Font large_font, small_font;
    if (!large_font.LoadFont("../rpi-rgb-led-matrix/fonts/9x15B.bdf")) {
        fprintf(stderr,"no large font\n"); return 1;
    }
    if (!small_font.LoadFont("../rpi-rgb-led-matrix/fonts/6x10.bdf")) {
        fprintf(stderr,"no small font\n"); return 1;
    }
    RGBMatrix *matrix = RGBMatrix::CreateFromOptions(opts,rt);
    if (!matrix) return 1;
    FrameCanvas *canvas = matrix->CreateFrameCanvas();
    signal(SIGTERM,InterruptHandler);
    signal(SIGINT, InterruptHandler);

    std::string token; long token_expiry = 0;
    const Color black(0,0,0), cyan(0,200,255), gray(120,120,120);

    while (!interrupt_received) {
        canvas->Fill(0,0,0);
        DrawText(canvas,large_font,50,18+large_font.baseline(),cyan,&black,"SCANNING",0);
        DrawText(canvas,small_font,50,36+small_font.baseline(),gray,&black,"finding nearby flight...",0);
        canvas = matrix->SwapOnVSync(canvas);

        LatLon loc = get_location();
        if (!loc.ok) { fprintf(stderr,"get_location failed\n"); sleep(3); continue; }

        if (token.empty() || time(nullptr) >= token_expiry) {
            token = fetch_token(client_id, client_secret);
            token_expiry = time(nullptr) + 1740;
        }
        if (token.empty()) { fprintf(stderr,"token failed\n"); sleep(3); continue; }

        auto candidates = find_nearby(loc.lat, loc.lon, token);
        fprintf(stderr,"found %zu candidates\n", candidates.size());
        if (candidates.empty()) { sleep(3); continue; }

        FlightData *fd = nullptr;
        bool fa_used = false;
        for (auto &c : candidates) {
            if (interrupt_received) break;
            fprintf(stderr,"  trying %s...\n", c.callsign.c_str());
            auto *cand = new FlightData(c.icao24, client_id, client_secret);
            cand->prime(c.state);
            if (!fa_used && !fa_key.empty()) {
                fprintf(stderr,"  [FA] using FlightAware for %s\n", c.callsign.c_str());
                cand->set_flightaware_key(fa_key);
                fa_used = true;
            }
            if (!fa_used && !fa_key.empty()) { cand->set_flightaware_key(fa_key); fa_used=true; }
            cand->refresh();
            const FlightState &s = cand->state();
            if (!s.origin_icao.empty() && !s.dest_icao.empty()) {
                double pct = compute_progress(s);
                if (pct > 0.0 && pct < 100.0) { fd = cand; break; }
            }
            delete cand;
        }
        if (!fd) { fprintf(stderr,"no routed flight found\n"); sleep(3); continue; }
        fprintf(stderr,"tracking %s  %s->%s\n",
                fd->state().callsign.c_str(),
                fd->state().origin_icao.c_str(),
                fd->state().dest_icao.c_str());

        auto logo = load_logo(fd->state().callsign, 30, 30);

        std::mutex mtx;
        std::atomic<bool> stop{false};
        std::thread t(refresh_loop, fd, &mtx, &stop);

        int on_ground_count = 0;
        while (!interrupt_received) {
            FlightState s;
            { std::lock_guard<std::mutex> lock(mtx); s = fd->state(); }
            if (s.on_ground) { if (++on_ground_count >= 3) break; }
            else              { on_ground_count = 0; }
            render(canvas, large_font, small_font, s, false, logo);
            canvas = matrix->SwapOnVSync(canvas);
            usleep(16667);
        }

        stop = true; t.join();
        if (interrupt_received) { delete fd; break; }

        { FlightState s; { std::lock_guard<std::mutex> lock(mtx); s = fd->state(); }
          render(canvas, large_font, small_font, s, true, logo);
          canvas = matrix->SwapOnVSync(canvas); }
        for (int i=0; i<10 && !interrupt_received; ++i) sleep(1);
        delete fd;
    }

    matrix->Clear(); delete matrix;
    curl_global_cleanup();
    return 0;
}
