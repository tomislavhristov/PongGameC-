// PongGameC++.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include <wiringPi.h> 
#include <wiringPiI2C.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <csignal>

using namespace std::chrono_literals;

// OLED ?????????
constexpr int OLED_ADDR = 0x3C;
constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 64;
constexpr int PADDLE_HEIGHT = 16;
constexpr int PADDLE_WIDTH = 4;
constexpr int BALL_SIZE = 4;

// ????? GPIO ?????? (WiringPi ?????????)
constexpr int BTN1_UP = 5;   // GPIO 24
constexpr int BTN1_DOWN = 4; // GPIO 23
constexpr int BTN2_UP = 3;   // GPIO 22
constexpr int BTN2_DOWN = 2; // GPIO 27

int fd;
int paddle1_y = OLED_HEIGHT / 2 - PADDLE_HEIGHT / 2;
int paddle2_y = OLED_HEIGHT / 2 - PADDLE_HEIGHT / 2;
int ball_x = OLED_WIDTH / 2;
int ball_y = OLED_HEIGHT / 2;
int ball_dx = 2, ball_dy = 1;
int score1 = 0, score2 = 0;

std::atomic<bool> running(true);
std::mutex game_mutex;
uint8_t buffer[OLED_WIDTH * OLED_HEIGHT / 8];
unsigned long lastDebounce[8] = { 0 };

void ssd1306_command(uint8_t c) {
    wiringPiI2CWriteReg8(fd, 0x00, c);
}

void ssd1306_data(uint8_t d) {
    wiringPiI2CWriteReg8(fd, 0x40, d);
}

void ssd1306_init() {
    ssd1306_command(0xAE);
    ssd1306_command(0xD5); ssd1306_command(0x80);
    ssd1306_command(0xA8); ssd1306_command(0x3F);
    ssd1306_command(0xD3); ssd1306_command(0x00);
    ssd1306_command(0x40);
    ssd1306_command(0x8D); ssd1306_command(0x14);
    ssd1306_command(0x20); ssd1306_command(0x00);
    ssd1306_command(0xA1); ssd1306_command(0xC8);
    ssd1306_command(0xDA); ssd1306_command(0x12);
    ssd1306_command(0x81); ssd1306_command(0xCF);
    ssd1306_command(0xD9); ssd1306_command(0xF1);
    ssd1306_command(0xDB); ssd1306_command(0x40);
    ssd1306_command(0xA4); ssd1306_command(0xA6);
    ssd1306_command(0xAF);
}

void clear_buffer() {
    std::fill(std::begin(buffer), std::end(buffer), 0);
}

void set_pixel(int x, int y, bool on) {
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) return;
    int index = x + (y / 8) * OLED_WIDTH;
    if (on) buffer[index] |= (1 << (y % 8));
    else buffer[index] &= ~(1 << (y % 8));
}

void draw_rect(int x, int y, int w, int h) {
    for (int i = x; i < x + w; ++i)
        for (int j = y; j < y + h; ++j)
            set_pixel(i, j, true);
}

void draw_game() {
    clear_buffer();
    draw_rect(0, paddle1_y, PADDLE_WIDTH, PADDLE_HEIGHT);
    draw_rect(OLED_WIDTH - PADDLE_WIDTH, paddle2_y, PADDLE_WIDTH, PADDLE_HEIGHT);
    draw_rect(ball_x, ball_y, BALL_SIZE, BALL_SIZE);
    for (int page = 0; page < 8; ++page) {
        ssd1306_command(0xB0 + page);
        ssd1306_command(0x00);
        ssd1306_command(0x10);
        for (int col = 0; col < OLED_WIDTH; ++col)
            ssd1306_data(buffer[col + OLED_WIDTH * page]);
    }
}

// ??????a ? debounce: ?????? HIGH, ?????? ??????? ??? ????????? ????? HIGH
bool debounce(int pin, int idx) {
    if (digitalRead(pin) == HIGH) {
        unsigned long now = millis();
        if (now - lastDebounce[idx] > 120) {
            lastDebounce[idx] = now;
            return true;
        }
    }
    return false;
}

void buttons_loop() {
    while (running) {
        {
            std::lock_guard<std::mutex> lock(game_mutex);
            if (debounce(BTN1_UP, 0) && paddle1_y > 0) {
                paddle1_y -= 4;
                std::cout << "BTN1_UP pressed, paddle1_y: " << paddle1_y << std::endl;
            }
            if (debounce(BTN1_DOWN, 1) && paddle1_y < OLED_HEIGHT - PADDLE_HEIGHT) {
                paddle1_y += 4;
                std::cout << "BTN1_DOWN pressed, paddle1_y: " << paddle1_y << std::endl;
            }
            if (debounce(BTN2_UP, 2) && paddle2_y > 0) {
                paddle2_y -= 4;
                std::cout << "BTN2_UP pressed, paddle2_y: " << paddle2_y << std::endl;
            }
            if (debounce(BTN2_DOWN, 3) && paddle2_y < OLED_HEIGHT - PADDLE_HEIGHT) {
                paddle2_y += 4;
                std::cout << "BTN2_DOWN pressed, paddle2_y: " << paddle2_y << std::endl;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void ball_loop() {
    while (running) {
        {
            std::lock_guard<std::mutex> lock(game_mutex);
            ball_x += ball_dx;
            ball_y += ball_dy;
            if (ball_y <= 0 || ball_y >= OLED_HEIGHT - BALL_SIZE) ball_dy *= -1;

            if (ball_x <= PADDLE_WIDTH) {
                if (ball_y + BALL_SIZE >= paddle1_y && ball_y <= paddle1_y + PADDLE_HEIGHT)
                    ball_dx *= -1;
                else { score2++; ball_x = 64; ball_y = 32; }
            }
            if (ball_x + BALL_SIZE >= OLED_WIDTH - PADDLE_WIDTH) {
                if (ball_y + BALL_SIZE >= paddle2_y && ball_y <= paddle2_y + PADDLE_HEIGHT)
                    ball_dx *= -1;
                else { score1++; ball_x = 64; ball_y = 32; }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}

void display_loop() {
    while (running) {
        {
            std::lock_guard<std::mutex> lock(game_mutex);
            draw_game();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void setup_gpio() {
    wiringPiSetup();
    pinMode(BTN1_UP, INPUT);
    pinMode(BTN1_DOWN, INPUT);
    pinMode(BTN2_UP, INPUT);
    pinMode(BTN2_DOWN, INPUT);

    // ?????????? ?????????? pull-up/down ?????????, ?????? ???? ??????
    pullUpDnControl(BTN1_UP, PUD_OFF);
    pullUpDnControl(BTN1_DOWN, PUD_OFF);
    pullUpDnControl(BTN2_UP, PUD_OFF);
    pullUpDnControl(BTN2_DOWN, PUD_OFF);
}

void handle_sigint(int) {
    running = false;
    std::cout << "\n???????...\n";
}

int main() {
    signal(SIGINT, handle_sigint);
    fd = wiringPiI2CSetup(OLED_ADDR);
    if (fd < 0) {
        std::cerr << "?????? ??? I2C\n";
        return 1;
    }
    ssd1306_init();
    setup_gpio();
    std::thread t1(buttons_loop);
    std::thread t2(ball_loop);
    std::thread t3(display_loop);
    t1.join(); t2.join(); t3.join();
    return 0;
}