#include "led-matrix.h"
#include "graphics.h"
#include "flight_data.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

using namespace rgb_matrix;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) { interrupt_received = true; }

// Background thread: refresh flight data every 30 seconds
static void refresh_loop(FlightData *fd, std::mutex *mtx) {
    while (!interrupt_received) {
        {
            std::lock_guard<std::mutex> lock(*mtx);
            fd->refresh();
        }
        for (int i = 0; i < 30 && !interrupt_received; ++i)
            sleep(1);
    }
}

int main(int argc, char *argv[]) {
    // Load credentials from environment (set via systemd EnvironmentFile)
    const char *client_id     = getenv("CLIENT_ID");
    const char *client_secret = getenv("CLIENT_SECRET");

    if (!client_id || !client_secret || !*client_id || !*client_secret) {
        fprintf(stderr, "Missing CLIENT_ID or CLIENT_SECRET environment variables\n");
        return 1;
    }

    // TODO: make icao24 configurable via web config UI
    // For now, hardcoded — replace with the actual transponder hex of your tracked flight
    FlightData flight("a0b1c2", client_id, client_secret);
    std::mutex flight_mtx;

    // Kick off background refresh immediately
    flight.refresh();
    std::thread refresh_thread(refresh_loop, &flight, &flight_mtx);

    // --- Matrix setup ---
    RGBMatrix::Options matrix_options;
    rgb_matrix::RuntimeOptions runtime_opt;

    matrix_options.rows         = 32;
    matrix_options.cols         = 64;
    matrix_options.chain_length = 3;
    matrix_options.parallel     = 2;
    runtime_opt.gpio_slowdown   = 4;

    rgb_matrix::ParseOptionsFromFlags(&argc, &argv, &matrix_options, &runtime_opt);

    Font large_font, small_font;
    if (!large_font.LoadFont("../rpi-rgb-led-matrix/fonts/9x15B.bdf")) {
        fprintf(stderr, "Could not load large font\n"); return 1;
    }
    if (!small_font.LoadFont("../rpi-rgb-led-matrix/fonts/6x10.bdf")) {
        fprintf(stderr, "Could not load small font\n"); return 1;
    }

    RGBMatrix *matrix = RGBMatrix::CreateFromOptions(matrix_options, runtime_opt);
    if (!matrix) return 1;

    FrameCanvas *canvas = matrix->CreateFrameCanvas();

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT,  InterruptHandler);

    const Color black(  0,   0,   0);
    const Color yellow(255, 215,   0);
    const Color white (255, 255, 255);
    const Color cyan  (  0, 200, 255);
    const Color gray  (120, 120, 120);
    const Color green ( 80, 220,  80);
    const Color red   (220,  60,  60);

    const int COL1 = 2,  COL2 = 66, COL3 = 130;
    const int ROW1 = 0,  ROW2 = 18, ROW3 = 36, ROW4 = 52;

    while (!interrupt_received) {
        FlightState s;
        {
            std::lock_guard<std::mutex> lock(flight_mtx);
            s = flight.state();
        }

        canvas->Fill(0, 0, 0);

        // --- Left: airline + callsign ---
        DrawText(canvas, large_font, COL1, ROW1 + large_font.baseline(),
                 yellow, &black, s.airline.c_str(), 0);
        DrawText(canvas, small_font, COL1, ROW2 + small_font.baseline(),
                 white, &black, s.callsign.c_str(), 0);
        DrawLine(canvas, COL1, ROW2 + 12, COL1 + 58, ROW2 + 12, gray);
        DrawText(canvas, small_font, COL1, ROW3 + small_font.baseline(),
                 gray, &black, s.aircraft_type.c_str(), 0);
        DrawText(canvas, small_font, COL1, ROW4 + small_font.baseline(),
                 s.valid ? green : gray, &black, s.valid ? "LIVE" : "WAIT", 0);

        // --- Middle: route ---
        DrawText(canvas, large_font, COL2, ROW1 + large_font.baseline(),
                 white, &black, s.origin_icao.empty() ? s.origin_name.c_str() : s.origin_icao.c_str(), 0);
        DrawText(canvas, small_font, COL2, ROW2 + small_font.baseline(),
                 gray, &black, s.origin_name.c_str(), 0);
        DrawText(canvas, small_font, COL2 + 18, ROW3 + small_font.baseline(),
                 yellow, &black, ">>>>", 0);
        DrawText(canvas, large_font, COL2, ROW4 - 2 + large_font.baseline(),
                 white, &black, s.dest_icao.empty() ? s.dest_name.c_str() : s.dest_icao.c_str(), 0);

        // --- Right: telemetry ---
        char alt_buf[32], spd_buf[32], trk_buf[32], vr_buf[32];
        snprintf(alt_buf, sizeof(alt_buf), "ALT %5.0fft", s.altitude_m * 3.28084f);
        snprintf(spd_buf, sizeof(spd_buf), "SPD %4.0fkts", s.speed_ms  * 1.94384f);
        snprintf(trk_buf, sizeof(trk_buf), "TRK %3.0fdeg", s.track_deg);
        snprintf(vr_buf,  sizeof(vr_buf),  "V/R %+.1fm/s", s.vertical_rate_ms);

        DrawText(canvas, small_font, COL3, ROW1 + small_font.baseline(), cyan,  &black, alt_buf, 0);
        DrawText(canvas, small_font, COL3, ROW2 + small_font.baseline(), cyan,  &black, spd_buf, 0);
        DrawText(canvas, small_font, COL3, ROW3 + small_font.baseline(), white, &black, trk_buf, 0);
        DrawText(canvas, small_font, COL3, ROW4 + small_font.baseline(),
                 s.vertical_rate_ms >= 0 ? green : red, &black, vr_buf, 0);

        canvas = matrix->SwapOnVSync(canvas);
        usleep(16667);
    }

    refresh_thread.join();
    matrix->Clear();
    delete matrix;
    return 0;
}
