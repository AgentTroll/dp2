#include <Wire.h>
#include <SparkFun_APDS9960.h>
#include <DFRobot_I2CMultiplexer.h>

// ---------------- Helper types -------------------
typedef enum {
    INITIAL,
    WAITING,
    TRANSITION
} state_t;

typedef enum {
    CORRECT,
    INCORRECT,
    UNKNOWN
} sensor_reading_t;

typedef enum {
    RED,
    GREEN,
    BLUE,
    NONE
} color_t;

typedef struct {
    int r;
    int g;
    int b;
} rgb_t;

// ------------------ Constants -----------------------
const int INITIAL_ROUND_DURATION = 5000;
const int ROUND_DURATION_MIN = 100;
const int INITIAL_DURATION_DELTA = 200;
const int DURATION_DELTA_MIN = 50;
const double DURATION_EXPONENT = 0.95;

const unsigned long ROUND_TRANSITION_DURATION = 1500;
const int GAME_DIVISIONS = 4;

const int COLOR_DELTA_THRESH = 175;
const int R_RETRY_THRESH = 100;

const int SAMPLE_THRESH = 5;
const int BUFFER_SIZE = 5;

// ----------------- Pin numbers ----------------------
const int LIGHT_PINS[] = {7, 8};

const int SPEAKER_PIN = A1;
const int BUTTON_PIN = 6;

const int I2C_PORTS[] = {0, 1, 2, 3};
const rgb_t LED_PINS[] = {{2, 4, 3}, {A2, 10, 11}, {A3, 9, 5}, {12, 13, A0}};

// ----------------- RGB controllers ------------------
SparkFun_APDS9960 APDS = SparkFun_APDS9960();
DFRobot_I2CMultiplexer I2C_MULTI(0x70);

// ------------------ Game state -----------------------
rgb_t ambient_rgb[] = {{}, {}, {}, {}};

long r_seed;
bool has_started = false;

// State machine variables
state_t state = INITIAL;
unsigned long cur_round_duration = INITIAL_ROUND_DURATION;
unsigned long cur_duration_delta = INITIAL_DURATION_DELTA;

unsigned long last_state_timestamp;
color_t color_settings[] = {NONE, NONE, NONE, NONE};

int buffer_idx[] = {0, 0, 0, 0};
color_t color_buf[4][BUFFER_SIZE] = {{}, {}, {}, {}};

// ----------------- Support functions ----------------
void debug_rgb(rgb_t rgb) {
    Serial.print(rgb.r);
    Serial.print(" ");
    Serial.print(rgb.g);
    Serial.print(" ");
    Serial.println(rgb.b);
}

// -------------- RGB sensor wrappers --------------
bool read_rgb(int port, rgb_t *rgb) {
    I2C_MULTI.selectPort(port);

    uint16_t r;
    uint16_t g;
    uint16_t b;
    if (!APDS.readRedLight(r) ||
        !APDS.readGreenLight(g) ||
        !APDS.readBlueLight(b)) {
        Serial.println("Error reading light values");
        return false;
    }

    rgb->r = r;
    rgb->g = g;
    rgb->b = b;

    return true;
}

color_t decode_color(rgb_t rgb) {
    if (rgb.r < COLOR_DELTA_THRESH
        && rgb.r > -COLOR_DELTA_THRESH
        && rgb.g < COLOR_DELTA_THRESH
        && rgb.g > -COLOR_DELTA_THRESH
        && rgb.b < COLOR_DELTA_THRESH
        && rgb.b > -COLOR_DELTA_THRESH) {
        return NONE;
    }

    if (rgb.r > rgb.g && rgb.r > rgb.b) {
        return RED;
    }

    if (rgb.g > rgb.b && rgb.g > rgb.r) {
        return GREEN;
    }

    if (rgb.b > rgb.g && rgb.b > rgb.r) {
        return BLUE;
    }

    return NONE;
}

void write_rgb(rgb_t pins, int r, int g, int b) {
    analogWrite(pins.r, r);
    analogWrite(pins.g, g);
    analogWrite(pins.b, b);
}

void write_color(int idx, color_t color) {
    rgb_t pins = LED_PINS[idx];
    switch (color) {
        case RED:
            write_rgb(pins, 255, 0, 0);
            break;
        case GREEN:
            write_rgb(pins, 0, 255, 0);
            break;
        case BLUE:
            write_rgb(pins, 0, 0, 255);
            break;
        case NONE:
            write_rgb(pins, 0, 0, 0);
            break;
    }
}

color_t buffer_color(int port, color_t input) {
    if (input == NONE) {
        return NONE;
    }
  
    int idx = buffer_idx[port] % BUFFER_SIZE;
    color_t *port_buffer = port_buffer = color_buf[port];
    port_buffer[idx] = input;

    int color_cnt[3] = {};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < BUFFER_SIZE; ++j) {
            if (port_buffer[i] == i) {
                color_cnt[i]++;
            }
        }
    }

    int max_color = NONE;
    int max_cnt = -1;
    for (int i = 0; i < 3; i++) {
        int cnt = color_cnt[i];
        if (cnt > max_cnt) {
            max_color = i;
            max_cnt = cnt;
        }
    }
  
    buffer_idx[port]++;

    if (max_color != NONE && max_cnt > SAMPLE_THRESH) {
        return max_color;
    }

    return NONE;
}

// ------------ State helper functions -------------
void randomize() {
    int available_idx[] = {0, 1, 2, 3};
    color_t available_colors[] = {RED, GREEN, BLUE};
    for (int i = 0; i < 3; ++i) {
        int r;
        int r_idx;
        color_t color;
        int idx;
        do {
            r = random() % 3;
            r_idx = random() % 4;
            
            color = available_colors[r];
            idx = available_idx[r_idx];
        } while (color == NONE || idx == -1);
        available_colors[r] = NONE;
        available_idx[r_idx] = -1;
        
        color_settings[idx] = color;
        write_color(idx, color);
    }
}

sensor_reading_t can_waiting_state_proceed() {
    color_t prev_reading = NONE;
    int correct_readings = 0;
    int incorrect_readings = 0;
    for (int port : I2C_PORTS) {
        rgb_t rgb = {};
        if (read_rgb(port, &rgb)) {
            rgb_t ambient = ambient_rgb[port];
            rgb_t delta_rgb = {rgb.r - ambient.r, rgb.g - ambient.g, rgb.b - ambient.b};
            
            color_t reading = decode_color(delta_rgb);
            // reading = buffer_color(port, reading);

            color_t setting = color_settings[port];
            if (reading != NONE) {
                if (setting != reading) {
                    incorrect_readings++;
                } else {
                    correct_readings++;
                }

                prev_reading = reading;
            } else if (setting == NONE) {
                correct_readings++;
            }
        }
    }

    if (correct_readings == 2) {
        return CORRECT;
    } else {
        return UNKNOWN;
    }
}

void clear_state() {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < BUFFER_SIZE; ++j) {
            color_buf[i][j] = NONE;
        }
    }
  
    for (int i = 0; i < GAME_DIVISIONS; ++i) {
        color_settings[i] = NONE;
        write_color(i, NONE);
    }
}

void reset_game() {
    clear_state();

    for (int port : I2C_PORTS) {
        read_rgb(port, ambient_rgb + port);
    }
  
    state = INITIAL;
    cur_round_duration = INITIAL_ROUND_DURATION;
    cur_duration_delta = INITIAL_DURATION_DELTA;
  
    delay(2000);
}

// ------------- Round begin/end functions ---------------
void play_begin_round_tone() {
    tone(SPEAKER_PIN, 4000, 500);
    Serial.println("START");
}

void play_correct_tone() {
    for (int i = 0; i < GAME_DIVISIONS; ++i) {
        write_color(i, GREEN);
    }
    
    Serial.println("CORRECT");
}

void play_incorrect_tone() {
    for (int i = 0; i < GAME_DIVISIONS; ++i) {
        write_color(i, RED);
    }
    
    Serial.println("INCORRECT");
}

// ---------------- Arduino control ------------------
void setup() {
    Serial.begin(9600);
    
    pinMode(SPEAKER_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT);

    for (int i = 0; i < GAME_DIVISIONS; ++i) {
        rgb_t pins = LED_PINS[i];
        pinMode(pins.r, OUTPUT);
        analogWrite(pins.r, 0);
        pinMode(pins.g, OUTPUT);
        analogWrite(pins.g, 0);
        pinMode(pins.b, OUTPUT);
        digitalWrite(pins.b, 0);
    }
    
    // Turn on the light LEDs
    for (int pin : LIGHT_PINS) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }

    // Initialize RGB sensors
    for (int port : I2C_PORTS) {
        I2C_MULTI.selectPort(port);
        if (!APDS.init()) {
            Serial.println("Failed to initialize the RGB sensor");
        }

        if (!APDS.enableLightSensor(false)) {
            Serial.println("Failed to initialize the RGB sensor");
        }

        // Wait for sensor calibration
        delay(500);
    }
    
    // Setup time seems to be "relatively" good at
    // adding entropy to the random seed
    r_seed = millis();
}

void loop() {
    // Await button press
    int button_reading = digitalRead(BUTTON_PIN);
    if (button_reading == HIGH) {
        has_started = true;

        // reset_game();
    }

    if (!has_started) {
        return;
    }

    unsigned long cur_time = millis();
    if (state == INITIAL) {
        randomize();
        play_begin_round_tone();

        last_state_timestamp = cur_time;
        state = WAITING;
    } else if (state == WAITING) {
        sensor_reading_t sr = can_waiting_state_proceed();

        // Wrong card(s), move on
        if (sr == INCORRECT) {
            play_incorrect_tone();
        }

        // All correct cards, move on
        if (sr == CORRECT) {
            play_correct_tone();
        }

        // Still waiting for them to put in another card
        if (sr == UNKNOWN) {
            unsigned long elapsed = cur_time - last_state_timestamp;
            if (elapsed > cur_round_duration) {
                play_incorrect_tone();
            } else {
                return;
            }
        }

        state = TRANSITION;
    } else if (state == TRANSITION) {
        delay(ROUND_TRANSITION_DURATION);
        clear_state();

        if (cur_round_duration > ROUND_DURATION_MIN) {
            cur_round_duration -= cur_duration_delta;

            if (cur_duration_delta > DURATION_DELTA_MIN) {
                cur_duration_delta = (int) (cur_duration_delta * DURATION_EXPONENT);
            }
        }

        state = INITIAL;
    }
}
